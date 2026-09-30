#include "mathx/Thermal.h"

#include <cmath>

namespace sd {

namespace {

float clampf(float v, float lo, float hi) {
    return (v < lo) ? lo : (v > hi) ? hi : v;
}

/// Time constant of the smoothed die temperature the plateau test reads. Long
/// enough that one noisy sample cannot end the climb, short enough not to lag
/// the climb it is watching.
constexpr float kSlowTauSec = 30.0f;

}  // namespace

// -----------------------------------------------------------------------------
const char* tempSourceName(TempSource s) {
    switch (s) {
        case TempSource::Soc:  return "SoC";
        case TempSource::Imu:  return "IMU";
        case TempSource::Pmic: return "PMIC";
        default:               return "?";
    }
}

const char* tempSourcePart(TempSource s) {
    switch (s) {
        case TempSource::Soc:  return "ESP32-S3";
        case TempSource::Imu:  return "BMI270";
        case TempSource::Pmic: return "M5PM1";
        default:               return "?";
    }
}

// -----------------------------------------------------------------------------
void ThermalReading::set(TempSource s, float celsius) {
    const int i = static_cast<int>(s);
    if (i < 0 || i >= kTempSourceCount) return;
    // A die that reports NaN did not report. Letting one through would poison
    // every comparison below, silently and in both directions.
    if (!(celsius > -100.0f && celsius < 200.0f)) return;
    c[i]  = celsius;
    ok[i] = true;
}

int ThermalReading::valid() const {
    int n = 0;
    for (bool v : ok) {
        if (v) ++n;
    }
    return n;
}

float ThermalReading::coolest() const {
    float best  = 0.0f;
    bool  found = false;
    for (int i = 0; i < kTempSourceCount; ++i) {
        if (!ok[i]) continue;
        if (!found || c[i] < best) { best = c[i]; found = true; }
    }
    return best;
}

float ThermalReading::hottest() const {
    float best  = 0.0f;
    bool  found = false;
    for (int i = 0; i < kTempSourceCount; ++i) {
        if (!ok[i]) continue;
        if (!found || c[i] > best) { best = c[i]; found = true; }
    }
    return best;
}

float ThermalReading::mean() const {
    float sum = 0.0f;
    int   n   = 0;
    for (int i = 0; i < kTempSourceCount; ++i) {
        if (!ok[i]) continue;
        sum += c[i];
        ++n;
    }
    return n ? (sum / static_cast<float>(n)) : 0.0f;
}

float ThermalReading::spread() const {
    if (valid() < 2) return 0.0f;
    return hottest() - coolest();
}

// -----------------------------------------------------------------------------
void ThermalModel::reset() {
    _die         = 0.0f;
    _rise        = 0.0f;
    _plateau     = kDefaultPlateauC;
    _learned     = false;
    _have        = false;
    _anchored    = false;
    _anchorC     = 0.0f;
    _started     = false;
    _maxClimb    = 0.0f;
    _slowDie     = 0.0f;
    _ratePerMin  = 0.0f;
    _plateauDone = false;
    _mayBeCold   = true;
}

void ThermalModel::beginSession(bool mayBeCold) {
    _mayBeCold   = mayBeCold;
    _maxClimb    = 0.0f;
    _anchored    = false;
    _started     = false;
    _plateauDone = false;
    _ratePerMin  = 0.0f;
}

void ThermalModel::setPlateau(float c, bool learned) {
    _plateau = clampf(c, 0.0f, kMaxPlateauC);
    _learned = learned;
}

float ThermalModel::riseC() const {
    // Bounded by what actually happened. The lag below predicts a climb toward
    // the plateau, and a board that has not climbed has not heated itself: one
    // switched on cold and left idle with the screen dim reaches nowhere near
    // the plateau, and subtracting one it never earned would walk the answer
    // steadily downward.
    //
    // Against the running MAXIMUM, not the current climb: carrying a warmed
    // board into a cold room drops the dies without undoing the heat in them.
    if (!_mayBeCold) return _rise;
    return (_rise < _maxClimb) ? _rise : _maxClimb;
}

void ThermalModel::update(const ThermalReading& r, float dt, uint32_t atSec) {
    if (r.valid() < 1) return;

    _die = r.mean();

    if (!_started) {
        _started     = true;
        _anchorC     = _die;
        _slowDie     = _die;
        _sinceAnchor = 0.0f;
        // A session that might have started cold begins with nothing
        // dissipated; one that certainly did not begins already warmed through.
        _rise = _mayBeCold ? 0.0f : _plateau;
        _have = true;
        return;
    }
    if (!(dt > 0.0f)) return;
    _sinceAnchor += dt;

    // -- the rise ------------------------------------------------------------
    // First-order lag toward the plateau. Modelled rather than read off the
    // climb, because the two differ where it matters: a board thirty seconds
    // off a cold start has climbed nothing and made none of its own heat, and
    // "how much of this did I do to myself" needs a small answer then and a
    // large one an hour later.
    _rise += (_plateau - _rise) * (1.0f - std::exp(-dt / kWarmupTauSec));

    // -- has this session proved itself cold? --------------------------------
    const float climb = _die - _anchorC;
    if (climb > _maxClimb) _maxClimb = climb;

    if (_mayBeCold && !_anchored) {
        if (_maxClimb >= kColdRiseC) {
            _anchored = true;
        } else if (_sinceAnchor >= static_cast<float>(kColdVerdictSec)) {
            // Long enough that a cold board would have climbed. It did not, so
            // it was already warm: drop the anchor and lean on the plateau.
            _mayBeCold = false;
            _rise      = _plateau;
        }
    }

    // -- measuring the plateau -----------------------------------------------
    const float prevSlow = _slowDie;
    _slowDie += (_die - _slowDie) * (1.0f - std::exp(-dt / kSlowTauSec));
    const float rate = (_slowDie - prevSlow) * (60.0f / dt);
    // Smoothed again: the rate is a difference of two smoothed numbers and is
    // noisy enough on its own to end the climb early by accident.
    _ratePerMin += (rate - _ratePerMin) * 0.1f;

    if (_anchored && !_plateauDone && _sinceAnchor >= static_cast<float>(kPlateauMinSec) &&
        _ratePerMin < kPlateauRateCPerMin) {
        learnPlateau(atSec);
    }
}

void ThermalModel::learnPlateau(uint32_t) {
    const float observed = clampf(_maxClimb, 0.0f, kMaxPlateauC);
    // The climb has stopped and the session started at the room's temperature,
    // so the whole of that climb was its own.
    _plateau     = _learned ? (_plateau + (observed - _plateau) * kPlateauBlend)
                            : observed;
    _learned     = true;
    _plateauDone = true;
    // The rise the model carries is now known to be exactly the climb, so it is
    // set rather than left to converge on the new plateau.
    _rise = observed;
}

bool overheatTrip(float hottestC, bool valid, float limitC, uint8_t& streak) {
    if (!valid) return false;
    if (hottestC < limitC) {
        streak = 0;
        return false;
    }
    if (streak < 0xFF) ++streak;
    return streak >= kOverheatSamples;
}

}  // namespace sd
