#include "services/ThermalService.h"

#include "core/Log.h"
#include "services/Settings.h"

#include <M5Unified.h>

#include <esp_system.h>

namespace sd {
namespace {

constexpr const char* kNs         = "thermal";
// Deliberately not "plateau". Every value written under that key was measured
// against a ceiling of 25 C and is clamped, and feeding one into learnPlateau's
// blend would take a device eight cold starts to crawl off a figure that was
// never true. The old key is left where it is and ignored.
constexpr const char* kKeyPlateau = "warmup";

// Nothing here needs calibrating any more. The one number a PERSON had to
// measure was the trim on the air estimate, and the air estimate is gone. The
// plateau stays, because the device learns that one by watching itself.

/// Below the BMI270's specified operating range, so nothing it says here is a
/// measurement. -41.0 exactly is its invalid-reading sentinel arriving
/// converted; a part genuinely colder than this has stopped being a sensor.
constexpr float kImuFloorC = -40.0f;

/// A learned constant is only worth writing back when it has moved. NVS is
/// flash, and this ticks every second for the life of the device.
constexpr float    kSaveDelta    = 0.05f;
constexpr uint32_t kSaveMinGapMs = 60000;

}  // namespace

// -----------------------------------------------------------------------------
void ThermalService::begin() {
    // Borrowed, not re-driven: M5Unified has already brought this bus up for
    // the IMU and the battery gauge, and installing a second driver on it is
    // the one way to break both.
    _pmicOk = (_pm1.begin(&m5::In_I2C) == M5PM1_OK);
    if (!_pmicOk) SD_LOGW("thermal", "PMIC did not answer");

    installSoc();

    load();
    // Told once, at the top, because after this the only evidence is thermal
    // and a restart that cooled nothing must never look like a cold start.
    _model.beginSession(startMayBeCold());
    _ready      = true;
    _lastReadMs = 0;
    _lastSaveMs = millis();
}

bool ThermalService::installSoc() {
    // Installed by us and not by Arduino's temperatureRead(), which owns a
    // process-wide singleton we cannot configure or reset. Only one of the two
    // can exist, so nothing in this firmware may call that function.
    temperature_sensor_config_t cfg =
        TEMPERATURE_SENSOR_CONFIG_DEFAULT(kSocRangeMinC, kSocRangeMaxC);
    if (temperature_sensor_install(&cfg, &_tsens) != ESP_OK) {
        _tsens = nullptr;
        SD_LOGW("thermal", "SoC sensor would not install");
        return false;
    }
    if (temperature_sensor_enable(_tsens) != ESP_OK) {
        temperature_sensor_uninstall(_tsens);
        _tsens = nullptr;
        SD_LOGW("thermal", "SoC sensor would not enable");
        return false;
    }
    return true;
}

void ThermalService::releaseSoc() {
    if (!_tsens) return;
    temperature_sensor_disable(_tsens);
    temperature_sensor_uninstall(_tsens);
    _tsens = nullptr;
}

void ThermalService::read(ThermalReading& out) {
    out = ThermalReading{};

    // Asked for only when it is worth asking. The driver logs an error of its
    // own on every failed call, so a sensor that has stopped answering costs a
    // line of serial output a second until something stops calling it.
    const bool due = (_socNextMs == 0) ||
                     static_cast<int32_t>(millis() - _socNextMs) >= 0;
    if (_tsens && due) {
        float socC = 0.0f;
        if (temperature_sensor_get_celsius(_tsens, &socC) == ESP_OK) {
            out.set(TempSource::Soc, socC);
            _socFails  = 0;
            _socNextMs = 0;
        } else {
            if (_socFails < 0xFFFF) ++_socFails;
            // Restarted on the tenth failure and every tenth after, which once
            // the cooldown is in force is one attempt every five minutes.
            if (_socFails % kSocRetryAfter == 0) {
                releaseSoc();
                if (installSoc()) ++_socRestarts;
                SD_LOGW("thermal", "SoC sensor restarted (%u)",
                        static_cast<unsigned>(_socRestarts));
            }
            if (_socFails >= kSocRetryAfter) _socNextMs = millis() + kSocCooldownMs;
        }
    }

    // The BMI270 answers 0x8000 for "no valid temperature": a part not in an
    // active mode has nothing to report. M5Unified does not check for it, and
    // runs the sentinel through the same 23 + adc/512 as any reading, handing
    // back exactly -41.0 C, which is inside set()'s sanity window and would go
    // straight into the mean. The part's specified range stops at -40, so
    // anything below that is the sentinel or worse.
    float imuC = 0.0f;
    if (M5.Imu.getTemp(&imuC) && imuC > kImuFloorC) out.set(TempSource::Imu, imuC);

    // Read but never set() into the model: this is millivolts, not degrees.
    // See the header for what went wrong when it was treated as the latter.
    _pmicRead = false;
    if (_pmicOk) {
        uint16_t mv = 0;
        if (_pm1.readTemperature(&mv) == M5PM1_OK) {
            _pmicMv   = mv;
            _pmicRead = true;
        }
    }
}

void ThermalService::tick(uint32_t nowMs) {
    if (!_ready) return;
    if (_lastReadMs != 0 && (nowMs - _lastReadMs) < kReadEverySec * 1000u) return;

    const float dt = (_lastReadMs == 0)
                         ? 0.0f
                         : static_cast<float>(nowMs - _lastReadMs) * 0.001f;
    _lastReadMs = nowMs;
    _upSec      = nowMs / 1000u;

    read(_last);
    _model.update(_last, dt, _upSec);
    guardOverheat();

    if (_model.plateauC() > _savedPlateau + kSaveDelta ||
        _model.plateauC() < _savedPlateau - kSaveDelta) {
        _dirty = true;
    }
    if (_dirty && (nowMs - _lastSaveMs) >= kSaveMinGapMs) save();
}

void ThermalService::guardOverheat() {
    if (!overheatTrip(_last.hottest(), _last.valid() > 0, kOverheatC, _hotStreak)) {
        return;
    }
    // Logged before anything else: after the next two lines there is no serial
    // port, and the reason the device went dark has to be somewhere.
    SD_LOGE("thermal", "%.1f C over the %.0f C limit - cutting power",
            static_cast<double>(_last.hottest()),
            static_cast<double>(kOverheatC));
    save();
    // Drops the board to L0 at the PMIC, and deep-sleeps if that fails. Either
    // way it does not come back here.
    M5.Power.powerOff();
}

bool ThermalService::pmicMillivolts(uint16_t& out) const {
    if (!_pmicRead) return false;
    out = _pmicMv;
    return true;
}

bool ThermalService::startMayBeCold() {
    switch (esp_reset_reason()) {
        case ESP_RST_SW:
        case ESP_RST_PANIC:
        case ESP_RST_INT_WDT:
        case ESP_RST_TASK_WDT:
        case ESP_RST_WDT:
        case ESP_RST_USB:
        case ESP_RST_JTAG:
        case ESP_RST_CPU_LOCKUP:
            // The board was running a moment ago, whatever the dies do next. A
            // reflash lands here, which is why this check exists.
            return false;
        default:
            return true;
    }
}

void ThermalService::forget() {
    _model.reset();
    _socRestarts = 0;
    _model.beginSession(startMayBeCold());
    save();
    SD_LOGI("thermal", "self-heating forgotten");
}

// -----------------------------------------------------------------------------
void ThermalService::load() {
    Settings st(kNs, true);
    if (!st.ok()) {
        _savedPlateau = _model.plateauC();
        return;
    }

    const float p = st.getFloat(kKeyPlateau, -1.0f);
    if (p >= 0.0f) _model.setPlateau(p, true);
    // Read back rather than kept: setPlateau clamps, and the saved copy has to
    // be what the model holds or every tick looks like a change.
    _savedPlateau = _model.plateauC();
}

void ThermalService::save() {
    Settings st(kNs, false);
    if (!st.ok()) return;
    // Only a MEASURED plateau is written. Storing the built-in guess would make
    // the next boot believe the device had calibrated itself.
    if (_model.plateauLearned()) st.putFloat(kKeyPlateau, _model.plateauC());
    _savedPlateau = _model.plateauC();
    _lastSaveMs = millis();
    _dirty      = false;
}

}  // namespace sd
