#pragma once
// -----------------------------------------------------------------------------
//  ThermalService - the three dies that know how hot they are.
//
//  The model in mathx/Thermal.h learns by watching the board go from cold to
//  warm, which happens in the first few minutes after switch-on, usually while
//  somebody is in the launcher or an unrelated app. If it only ran while its
//  own screen was open it would never see a cold board. So it ticks from
//  AppManager's loop beside the battery poll, and the screen is a window on it.
//
//  None of these sensors needs anything acquired, which is the only reason this
//  can run all the time:
//
//    SoC    the S3's own tsens peripheral, installed here rather than through
//           Arduino's temperatureRead(). That function owns a process-wide
//           singleton configured for 10-50 C, which resolves to the -10..80
//           range, and this board idles at seventy. It also cannot be reset:
//           the handle is static and install refuses a second one. Owning it
//           buys the range, the ability to reinstall a sensor that has stopped
//           answering, and a count of how often that has happened.
//    IMU    the BMI270's temperature register. ImuService::end() only resets
//           the sample rate, so the part keeps answering whether or not an app
//           owns it: the same opening MoodService uses to watch motion.
//
//  There is no ambient temperature here. The model behind it could not be made
//  honest on this board; see mathx/Thermal.h. What is left is the dies as read
//  and the self-heating as measured. The manual trim went with it, having
//  existed to nudge an estimate that no longer exists.
//
//  It also carries the overheat guard. The board has no cooling, so the only
//  answer to a die that keeps climbing is to stop being a load: past
//  kOverheatC for three readings running, this saves what it learned and cuts
//  power at the PMIC. Nothing else in the firmware watches temperature, which
//  is why the guard lives beside the one thing that reads it every second.
//
//  The PMIC is not one of them. M5PM1::readTemperature() is documented as
//  returning tenths of a degree, "250 means 25.0", and does not: its body is
//  `return analogRead(M5PM1_ADC_CH_TEMP, value)`, the raw converter reading in
//  millivolts. Around fourteen hundred of them became a hundred and forty
//  degrees, slipped past a sanity check that only refused values above two
//  hundred, and from there set the spread over a hundred, pinned confidence at
//  nothing and dragged the answer twenty degrees below the room.
//
//  Turning millivolts into degrees needs the part's own transfer function, and
//  the M5PM1 publishes none. So the reading is kept, shown as what it is, and
//  excluded from the model. It is the wrong die to ask anyway: the PMIC is the
//  charger, and it is hot precisely when it is working.
// -----------------------------------------------------------------------------
#include "mathx/Thermal.h"

#include <M5PM1.h>
#include <driver/temperature_sensor.h>

#include <cstdint>

namespace sd {

class ThermalService {
public:
    /// Die temperatures move over minutes. Once a second is already far more
    /// often than the answer changes, and each read is an I2C transaction on a
    /// bus other things want.
    static constexpr uint32_t kReadEverySec = 1;

    /// The span the SoC sensor is configured for.
    ///
    /// Put where the board actually LIVES: measured idle is near 70 C, so
    /// 20-100 resolves to the attribute covering it and the common case needs
    /// no range switch. The driver auto-ranges either side, which handles a
    /// cold start.
    static constexpr int kSocRangeMinC = 20;
    static constexpr int kSocRangeMaxC = 100;

    /// Consecutive failures before the sensor is torn down and put back.
    ///
    /// It does fail, persistently, and not for a reason the driver's sources
    /// explain: the value it rejects is outside -40..125, which a die at
    /// seventy is not. A peripheral that has stopped answering ten times
    /// running is worth restarting rather than asking again every second.
    static constexpr uint16_t kSocRetryAfter = 10;
    /// And how long to leave it alone once it is clearly not answering. The
    /// driver logs an error on every failed call, so asking a dead sensor costs
    /// a line of serial output a second.
    static constexpr uint32_t kSocCooldownMs = 30000;

    void begin();
    void tick(uint32_t nowMs);

    /// The limit the guard fires at, for a screen that has to tell somebody the
    /// device switches itself off.
    static constexpr float overheatLimitC() { return kOverheatC; }

    const ThermalReading& reading() const { return _last; }
    const ThermalModel&   model() const { return _model; }

    /// The PMIC's converter reading in millivolts, uncalibrated, or false when
    /// it did not answer. Shown rather than used; see the note above.
    ///
    /// Worth watching even so. An ADC channel a charger calls TEMP is almost
    /// always the pack thermistor, and if this sits still while the dies climb
    /// forty degrees, that alone says the cell is nowhere near them.
    bool pmicMillivolts(uint16_t& out) const;

    /// Forget the measured self-heating and start again.
    void forget();

    /// How many times the SoC sensor has been restarted, and whether it is
    /// answering now. Shown rather than hidden: a thermal reading built from
    /// one die instead of two is a different measurement.
    uint32_t socRestarts() const { return _socRestarts; }
    bool     socAlive() const { return _socFails == 0; }

    void save();

private:
    void load();
    void read(ThermalReading& out);
    /// Cuts power when the dies have been over kOverheatC for long enough. Does
    /// not return when it fires.
    void guardOverheat();
    bool installSoc();
    void releaseSoc();

    M5PM1                       _pm1;
    temperature_sensor_handle_t _tsens = nullptr;

    ThermalReading _last{};
    ThermalModel   _model;

    /// Whether the restart itself allows for the board having been cold. A
    /// panic, a watchdog or a reflash says it was already running.
    static bool startMayBeCold();

    uint8_t  _hotStreak   = 0;   ///< consecutive over-limit readings

    uint16_t _socFails    = 0;   ///< consecutive, reset by any good read
    uint32_t _socRestarts = 0;
    uint32_t _socNextMs   = 0;   ///< 0 = ask every read

    bool     _pmicOk    = false;
    uint16_t _pmicMv    = 0;
    bool     _pmicRead  = false;
    bool     _ready     = false;
    uint32_t _lastReadMs = 0;
    uint32_t _lastSaveMs = 0;
    uint32_t _upSec      = 0;
    float    _savedPlateau = 0.0f;
    bool     _dirty      = false;
};

}  // namespace sd
