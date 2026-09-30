#include "mathx/Words.h"

#include <cstdio>

namespace sd {

void sayDuration(uint32_t sec, char* out, size_t n) {
    if (sec < kSecondsUntil)     std::snprintf(out, n, "%lus", (unsigned long)sec);
    else if (sec < kMinutesUntil) std::snprintf(out, n, "%lu min", (unsigned long)(sec / 60u));
    else if (sec < kHoursUntil)   std::snprintf(out, n, "%lu h", (unsigned long)(sec / 3600u));
    else                          std::snprintf(out, n, "%lu days", (unsigned long)(sec / 86400u));
}

void sayDurationLong(uint32_t sec, char* out, size_t n) {
    if (sec < kSecondsUntil) {
        std::snprintf(out, n, "%lu s", (unsigned long)sec);
    } else if (sec < kMinutesUntil) {
        std::snprintf(out, n, "%lu min", (unsigned long)(sec / 60u));
    } else if (sec < kHoursUntil) {
        std::snprintf(out, n, "%lu h %lu m", (unsigned long)(sec / 3600u),
                      (unsigned long)((sec % 3600u) / 60u));
    } else {
        std::snprintf(out, n, "%lu d %lu h", (unsigned long)(sec / 86400u),
                      (unsigned long)((sec % 86400u) / 3600u));
    }
}

void sayAge(uint32_t ms, char* out, size_t n) {
    const uint32_t s = ms / 1000u;
    if (s < 60u)         std::snprintf(out, n, "%lus", (unsigned long)s);
    else if (s < 3600u)  std::snprintf(out, n, "%lum", (unsigned long)(s / 60u));
    else if (s < 86400u) std::snprintf(out, n, "%luh", (unsigned long)(s / 3600u));
    else                 std::snprintf(out, n, "%lud", (unsigned long)(s / 86400u));
}

// -----------------------------------------------------------------------------
namespace {

/// Q, X and Y are left out: alternated with a vowel they give syllables nobody
/// reads aloud the same way twice, which is the one thing a spoken label has to
/// get right. The five vowels are all usable.
constexpr char kCons[] = "BCDFGHJKLMNPRSTVWZ";
constexpr char kVows[] = "AEIOU";
constexpr int  kConsN  = static_cast<int>(sizeof(kCons)) - 1;
constexpr int  kVowsN  = static_cast<int>(sizeof(kVows)) - 1;

}  // namespace

void sayCode(uint32_t seed, char* out) {
    if (!out) return;
    // A byte per letter, so the whole seed is used rather than its low bits.
    out[0] = kCons[(seed >> 0) % kConsN];
    out[1] = kVows[(seed >> 8) % kVowsN];
    out[2] = kCons[(seed >> 16) % kConsN];
    out[3] = kVows[(seed >> 24) % kVowsN];
}

}  // namespace sd
