#pragma once
// -----------------------------------------------------------------------------
//  PowerService - battery, charge state, and the EXT_5V rail.
//
//  M5Unified leaves EXT_5V disabled at boot, which also removes power from the
//  IR transmitter and receiver. Rather than let each app remember that, the
//  rail is refcounted here and driven by the capability system.
// -----------------------------------------------------------------------------
#include <cstdint>

namespace sd {

class PowerService {
public:
    void begin();

    /// Cached; refreshed by refresh(). Battery reads are slow and noisy, so
    /// they happen once a second rather than once a frame.
    int   percent() const { return _pct; }
    float volts() const   { return _mv / 1000.0f; }
    bool  charging() const { return _charging; }

    void refresh(uint32_t nowMs);

    /// Refcounted so two consumers of the 5V rail cannot switch it off under
    /// one another.
    void acquireExt5V();
    void releaseExt5V();
    bool ext5vOn() const { return _ext5vRefs > 0; }

    /// The 5 V INPUT, in volts: whether an external supply is present, not what
    /// the boost is delivering.
    ///
    /// Worth being precise about, because the obvious reading is wrong. The
    /// PMIC's documentation notes that its 5VINOUT sense goes invalid while the
    /// boost is enabled, so on battery it reads near zero however healthy the
    /// boost is. It answers "is there external 5 V", not "can the rail carry
    /// this load", and nothing on this board answers the latter.
    float ext5vInVolts();

    /// Whether the PMIC's 5 V boost is switched on.
    bool ext5vBoostOn();

private:
    int      _pct       = -1;
    int      _mv        = 0;
    bool     _charging  = false;
    uint32_t _lastMs    = 0;
    int      _ext5vRefs = 0;
};

}  // namespace sd
