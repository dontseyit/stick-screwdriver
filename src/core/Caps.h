#pragma once
// -----------------------------------------------------------------------------
//  Hardware capabilities.
//
//  This is what stops one app breaking another. The StickS3 has mutually
//  hostile peripherals: the IR receiver does not work while the speaker
//  amplifier is powered, and IR needs EXT_5V switched on where M5Unified
//  switched it off. Each app declares what it needs and ServiceHub owns the
//  arbitration in one place.
//
//  Add a peripheral -> add a bit here and a case in ServiceHub::applyCaps().
//  No existing app changes, and none can get the sequencing wrong.
// -----------------------------------------------------------------------------
#include <cstdint>

namespace sd {

enum class Cap : uint32_t {
    None     = 0,
    Display  = 1u << 0,
    Imu      = 1u << 1,
    Speaker  = 1u << 2,
    Mic      = 1u << 3,
    IrTx     = 1u << 4,
    IrRx     = 1u << 5,
    Ext5V    = 1u << 6,  ///< power on the Grove / Hat rails (implied by IR)
    PortA    = 1u << 7,  ///< external I2C on G9/G10
    WiFi     = 1u << 8,
    Ble      = 1u << 9,
};

using CapMask = uint32_t;

constexpr CapMask operator|(Cap a, Cap b) {
    return static_cast<CapMask>(a) | static_cast<CapMask>(b);
}
constexpr CapMask operator|(CapMask a, Cap b) { return a | static_cast<CapMask>(b); }
constexpr CapMask caps(Cap a) { return static_cast<CapMask>(a); }

inline CapMask& operator|=(CapMask& a, Cap b) { return a = a | b; }

constexpr bool has(CapMask m, Cap c) {
    return (m & static_cast<CapMask>(c)) != 0;
}

/// What a request means once the board's own hostilities are applied: implied
/// bits are added, and bits it cannot have alongside what it asked for struck.
///
/// It lives here rather than in the hub that calls it because it is the only
/// part of the arbitration that states something about the hardware rather than
/// acting on it: nothing is begun or ended, and the same mask in always gives
/// the same mask out. That is what makes it checkable on a host; see
/// test/test_caps.
///
/// `libraryMode` is passed rather than read, because the rule is about the
/// setting's value, not where it is kept. The hub reads it from
/// services/SystemSettings.h and hands it over.
CapMask expandImplied(CapMask want, bool libraryMode);

}  // namespace sd
