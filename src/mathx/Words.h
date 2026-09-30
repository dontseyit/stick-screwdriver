#pragma once
// -----------------------------------------------------------------------------
//  Words - spans of time, for someone reading a small screen at a glance.
//
//  Two shapes, because the distinction is real: a log column has three
//  characters to spend and a stats row does not. What is not duplicated is the
//  judgement about where seconds stop being useful and minutes start, which is
//  decided once here and tested.
//
//  Hardware-free; unit-tested on the host.
// -----------------------------------------------------------------------------
#include <cstddef>
#include <cstdint>

namespace sd {

/// Where each unit gives way to the next. Ninety seconds rather than sixty so
/// "80s" is available: the minute after the first is the one people most often
/// want precisely. An hour and a half before hours, two days before days.
constexpr uint32_t kSecondsUntil = 90u;
constexpr uint32_t kMinutesUntil = 5400u;
constexpr uint32_t kHoursUntil   = 172800u;

/// The shortest true thing: "45s", "12 min", "3 h", "2 days".
void sayDuration(uint32_t sec, char* out, size_t n);

/// The same spans, keeping the finer unit: "45 s", "12 min", "3 h 12 m",
/// "2 d 4 h".
void sayDurationLong(uint32_t sec, char* out, size_t n);

/// How long ago, in the width of a log column: "45s", "12m", "3h", "2d".
void sayAge(uint32_t ms, char* out, size_t n);

/// How many codes sayCode can spell. Four letters alternating consonant and
/// vowel: 18 * 5 * 18 * 5.
constexpr uint32_t kCodeSpace = 8100u;

/// Four pronounceable letters for `seed`, written to `out[0..3]` with no
/// terminator.
///
/// This device has no clock, so a thing that happened cannot be named by when.
/// A label with no meaning is honest about having none, and it is still what
/// you say out loud: "BOKA", not "record 3" and not a fictional timestamp.
/// Consonants and vowels alternate so every code can be pronounced.
///
/// Deterministic in the seed. The caller owns where the seed comes from and
/// whether a collision matters, because only the caller knows what else is
/// already named. kCodeSpace says how much room there is to collide in.
void sayCode(uint32_t seed, char* out);

}  // namespace sd
