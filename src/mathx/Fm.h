#pragma once
// -----------------------------------------------------------------------------
//  Fm - two-operator FM synthesis, rendered a note at a time.
//
//  tone() plays a fixed 16-sample wavetable at whatever rate makes the pitch,
//  so every note has the same timbre and the same envelope: on, then off. There
//  is nothing there for a brightness or a decay control to act on.
//
//  Two-operator FM gives both from three numbers: a modulator at some ratio of
//  the carrier frequency, an index saying how hard it modulates, and an
//  envelope. Index near zero is a sine, index high is a bell or a metallic
//  clang, and the envelope turns one voice from a pluck into a pad. It renders
//  in a millisecond on this device.
//
//  The trap is aliasing, which sounds like brightness until it does not. FM
//  produces sidebands at the carrier plus and minus multiples of the modulator,
//  and Carson's rule puts the occupied bandwidth near 2*(index + 1)*modulator.
//  This board outputs at 22050 Hz, so anything past 11025 Hz folds back and
//  reappears at a frequency unrelated to the note. At low index it genuinely
//  sounds brighter, and it degrades gradually rather than breaking, so the
//  index is clamped by what the carrier can afford: plenty of room low, almost
//  none high. Real instruments darken as they ascend, so this reads as natural.
//
//  Hardware-free; unit-tested on the host.
// -----------------------------------------------------------------------------
#include <cstddef>
#include <cstdint>

namespace sd {

/// What the StickS3's I2S runs at. M5Unified sets it for this board and
/// everything here has to agree.
constexpr float kSynthSampleHz = 22050.0f;

/// Modulator frequency as a multiple of the carrier. 1:1 is harmonic and warm,
/// 0.5 is an octave down and hollow, 3.5 is inharmonic and bell-like.
constexpr int   kFmRatioCount = 3;
extern const float       kFmRatios[kFmRatioCount];
extern const char* const kFmRatioNames[kFmRatioCount];

/// The largest modulation index this carrier can carry without folding energy
/// back down the spectrum, by Carson's rule against Nyquist. Returns 0 when
/// even an unmodulated carrier is too high to be safe.
float fmSafeIndex(float carrierHz, float ratio, float sampleHz = kSynthSampleHz);

/// Render one note into `out`, and return how many samples were written.
///
/// The envelope is an exponential decay from full scale, which is what a struck
/// or plucked source does. `index` is clamped by fmSafeIndex() before use, so a
/// caller cannot ask for aliasing.
///
/// `amplitude` is 0..1 of full scale. The buffer must hold `capacity` samples;
/// the render stops at whichever of the decay or the capacity ends first.
size_t fmRenderNote(int16_t* out, size_t capacity, float carrierHz, float ratio,
                    float index, float decaySec, float amplitude,
                    float sampleHz = kSynthSampleHz);

/// Samples a note of this length needs, for sizing a buffer.
constexpr size_t fmNoteSamples(float decaySec, float sampleHz = kSynthSampleHz) {
    return static_cast<size_t>(decaySec * sampleHz);
}

}  // namespace sd
