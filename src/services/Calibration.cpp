#include "services/Calibration.h"

#include "core/Log.h"
#include "services/Settings.h"

#include <cstdio>

namespace sd {
namespace {

constexpr const char* kNs = "calib";

/// Suffix appended to a key for its "somebody measured this" flag. A separate
/// entry rather than a sentinel value, because every sentinel worth choosing is
/// also a legal calibration.
constexpr const char* kSetSuffix = "!";

const CalSpec kSpecs[kCalCount] = {
    {
        .key      = "micdb",
        .name     = "microphone",
        .part     = "ES8311",
        .quantity = "dBFS to dB SPL",
        .unit     = "dB",
        // An idle room reads about -60 dBFS on this hardware at unity gain and
        // a quiet room is roughly 35 dB SPL, putting full scale near 95 dB
        // above the reading. Good to maybe +/-10 dB: enough that an
        // uncalibrated level is plausible rather than a negative number.
        .nominal  = 95.0f,
        .lo       = 40.0f,
        .hi       = 140.0f,
        // Not adjustable blind. The offset is not a quantity anyone knows: you
        // dial the number the reference meter shows, which needs a live level
        // next to it.
        .step     = 0.0f,
        .where    = "ANALYSER or ROOMS, level page",
    },
};

}  // namespace

// -----------------------------------------------------------------------------
Calibration& Calibration::instance() {
    static Calibration c;
    return c;
}

const CalSpec& Calibration::spec(CalId id) const {
    const int i = static_cast<int>(id);
    return kSpecs[(i >= 0 && i < kCalCount) ? i : 0];
}

void Calibration::begin() {
    if (_ready) return;
    for (int i = 0; i < kCalCount; ++i) {
        _value[i] = kSpecs[i].nominal;
        _set[i]   = false;
    }
    load();
    migrate();
    _ready = true;
}

float Calibration::value(CalId id) const {
    const int i = static_cast<int>(id);
    if (i < 0 || i >= kCalCount) return 0.0f;
    return _value[i];
}

bool Calibration::measured(CalId id) const {
    const int i = static_cast<int>(id);
    return (i >= 0 && i < kCalCount) && _set[i];
}

int Calibration::measuredCount() const {
    int n = 0;
    for (bool b : _set) {
        if (b) ++n;
    }
    return n;
}

void Calibration::set(CalId id, float v) {
    const int i = static_cast<int>(id);
    if (i < 0 || i >= kCalCount) return;
    const CalSpec& sp = kSpecs[i];
    _value[i] = (v < sp.lo) ? sp.lo : (v > sp.hi) ? sp.hi : v;
    _set[i]   = true;
    save(id);
}

void Calibration::clear(CalId id) {
    const int i = static_cast<int>(id);
    if (i < 0 || i >= kCalCount) return;
    _value[i] = kSpecs[i].nominal;
    _set[i]   = false;
    save(id);
}

void Calibration::nudge(CalId id, int steps) {
    const int i = static_cast<int>(id);
    if (i < 0 || i >= kCalCount) return;
    const CalSpec& sp = kSpecs[i];
    if (!(sp.step > 0.0f)) return;
    set(id, _value[i] + sp.step * static_cast<float>(steps));
}

// -----------------------------------------------------------------------------
void Calibration::load() {
    Settings s(kNs, /*readOnly*/ true);
    if (!s.ok()) return;
    for (int i = 0; i < kCalCount; ++i) {
        char flag[20];
        std::snprintf(flag, sizeof(flag), "%s%s", kSpecs[i].key, kSetSuffix);
        if (!s.getBool(flag, false)) continue;
        _value[i] = s.getFloat(kSpecs[i].key, kSpecs[i].nominal);
        _set[i]   = true;
    }
}

void Calibration::save(CalId id) {
    Settings s(kNs, /*readOnly*/ false);
    if (!s.ok()) return;
    const int      i  = static_cast<int>(id);
    const CalSpec& sp = kSpecs[i];
    char           flag[20];
    std::snprintf(flag, sizeof(flag), "%s%s", sp.key, kSetSuffix);
    s.putFloat(sp.key, _value[i]);
    s.putBool(flag, _set[i]);
}

/// Values that were calibrated before this store existed.
///
/// Read once and written here, then left alone: the old keys are not deleted,
/// so downgrading to a build without this file finds its calibration intact.
/// They are consulted only when this store has nothing, so a later calibration
/// here is never overwritten by the stale copy over there.
void Calibration::migrate() {
    if (!_set[static_cast<int>(CalId::MicOffsetDb)]) {
        Settings s("analyser", /*readOnly*/ true);
        if (s.ok() && s.getBool("caldone", false)) {
            const float v = s.getFloat("cal", 0.0f);
            // The old store kept the offset relative to nothing when unset, so
            // only a genuinely calibrated one is worth carrying over.
            if (v > 0.0f) {
                set(CalId::MicOffsetDb, v);
                SD_LOGI("calib", "took the microphone offset from ANALYSER");
            }
        }
    }
}

// -----------------------------------------------------------------------------
float micSpl(float dbfs) {
    return dbfs + calibration().value(CalId::MicOffsetDb);
}

bool micCalibrated() { return calibration().measured(CalId::MicOffsetDb); }

}  // namespace sd
