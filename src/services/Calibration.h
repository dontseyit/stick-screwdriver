#pragma once
// -----------------------------------------------------------------------------
//  Calibration - the numbers a person had to measure, in one place.
//
//  A calibration is a constant this device CANNOT work out for itself, because
//  discovering it needs a reference the device does not have: a sound level
//  meter, a real thermometer, a known angle. Somebody has to bring one, take a
//  reading, and tell it the answer.
//
//  That is a different kind of value from one the device learns by watching
//  itself, such as the thermal plateau or the room references, and the two must
//  not be stored together. A learned value belongs to whatever learns it, is
//  re-derivable at any time, and is nobody else's business. A calibration is
//  entered once, is expensive to obtain again, and is read by anything wanting
//  an absolute number out of that sensor.
//
//  It is central rather than per-app because the microphone's sensitivity is a
//  fact about the microphone. Learned in ANALYSER, which happened to have a
//  live meter on screen, and stored in ANALYSER's own settings, it was where
//  ROOMS could not see it: two apps reading one microphone disagreeing about
//  how loud the world is, and the second asking the user to calibrate a
//  microphone that already was.
//
//  Adding one: a row in the table in Calibration.cpp and an entry in CalId.
//  Whatever consumes it reads value(); whatever performs it calls set(). The
//  CALIBRATION app picks it up without being touched.
// -----------------------------------------------------------------------------
#include <cstdint>

namespace sd {

enum class CalId : uint8_t {
    /// dBFS to dB SPL for the built-in microphone. Set against a sound level
    /// meter; a phone app is close enough to be worth far more than nothing.
    MicOffsetDb,
    // A thermometer trim lived here. It offset an estimated air temperature,
    // and that estimate has been retired; see mathx/Thermal.h. Its NVS key
    // ("thermc") is burned rather than reused: a key is the record of what was
    // measured, and pointing an old one at a new quantity would silently import
    // somebody's degrees as something else.
    Count,
};

constexpr int kCalCount = static_cast<int>(CalId::Count);

struct CalSpec {
    /// NVS key. Never change one once it has shipped: it is the record.
    const char* key;
    const char* name;      ///< "microphone"
    const char* part;      ///< "ES8311"
    const char* quantity;  ///< what the number converts
    const char* unit;
    /// In force until somebody measures it. Chosen so an uncalibrated reading
    /// is a plausible number rather than an obviously broken one, never so that
    /// it can be mistaken for a measurement.
    float nominal;
    float lo, hi;
    /// Step for adjusting it from the CALIBRATION app, or 0 for a value that
    /// cannot sensibly be dialled without a live instrument in front of you.
    float step;
    /// Where it is performed when it needs an instrument this app has not got.
    const char* where;
};

class Calibration {
public:
    static Calibration& instance();

    /// Loads every value, and migrates any that used to live elsewhere. Called
    /// once from ServiceHub::begin(), before anything that reads one.
    void begin();

    const CalSpec& spec(CalId id) const;

    /// The measured value, or the nominal when nobody has measured it.
    float value(CalId id) const;
    /// Whether the number above came from a reference or from the table.
    bool measured(CalId id) const;

    void set(CalId id, float v);
    void clear(CalId id);
    /// Adjust by whole steps, clamped. Marks the value measured.
    void nudge(CalId id, int steps);

    /// How many have been measured, for a screen that wants one number.
    int measuredCount() const;

private:
    Calibration() = default;

    void load();
    void migrate();
    void save(CalId id);

    float _value[kCalCount]{};
    bool  _set[kCalCount]{};
    bool  _ready = false;
};

inline Calibration& calibration() { return Calibration::instance(); }

/// dBFS to dB SPL, using whatever the microphone calibration currently holds.
float micSpl(float dbfs);
/// Whether that number is entitled to be printed without a qualifier.
bool micCalibrated();

}  // namespace sd
