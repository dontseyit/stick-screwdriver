#pragma once
// -----------------------------------------------------------------------------
//  One palette for the whole device. Apps reference roles, never raw colours,
//  so the look stays coherent as apps are added and restyles in one edit.
// -----------------------------------------------------------------------------
#include <cstdint>

namespace sd::ui {

/// 24-bit RGB -> RGB565, at compile time.
constexpr uint16_t rgb(uint8_t r, uint8_t g, uint8_t b) {
    return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

namespace theme {

constexpr uint16_t kBg      = rgb(0x0A, 0x0C, 0x10);  // near-black ground
constexpr uint16_t kSurface = rgb(0x16, 0x1A, 0x22);  // panels, vials
constexpr uint16_t kLine    = rgb(0x2A, 0x31, 0x3D);  // hairlines, ticks
constexpr uint16_t kText    = rgb(0xE6, 0xED, 0xF3);
constexpr uint16_t kDim     = rgb(0x7D, 0x85, 0x90);
constexpr uint16_t kFaint   = rgb(0x4A, 0x52, 0x5E);

constexpr uint16_t kAccent  = rgb(0xFF, 0xB3, 0x00);  // sonic amber
constexpr uint16_t kCyan    = rgb(0x39, 0xC5, 0xCF);
constexpr uint16_t kOk      = rgb(0x3F, 0xB9, 0x50);
constexpr uint16_t kWarn    = rgb(0xD2, 0x99, 0x22);
constexpr uint16_t kErr     = rgb(0xF8, 0x51, 0x49);

constexpr uint16_t kBarH    = 14;  ///< status bar height, px
constexpr uint16_t kFootH   = 12;  ///< footer hint height, px

}  // namespace theme

/// A colour `t` of the way from `a` to `b`, in RGB565.
///
/// Beside the palette because it is what the palette is for: a role is a fixed
/// point, and anything drawn between two of them (a blade tint, a fading ping,
/// a hue tracking a measurement) is a mix. Unpacked per channel rather than
/// interpolated on the packed value, which would carry the green field's low
/// bits into red.
inline uint16_t mix565(uint16_t a, uint16_t b, float t) {
    if (t <= 0.0f) return a;
    if (t >= 1.0f) return b;
    const int ar = (a >> 11) & 0x1F, ag = (a >> 5) & 0x3F, ab = a & 0x1F;
    const int br = (b >> 11) & 0x1F, bg = (b >> 5) & 0x3F, bb = b & 0x1F;
    const int r  = ar + static_cast<int>((br - ar) * t + 0.5f);
    const int g  = ag + static_cast<int>((bg - ag) * t + 0.5f);
    const int bl = ab + static_cast<int>((bb - ab) * t + 0.5f);
    return static_cast<uint16_t>((r << 11) | (g << 5) | bl);
}

}  // namespace sd::ui
