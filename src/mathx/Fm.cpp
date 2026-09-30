#include "mathx/Fm.h"

#include <cmath>

namespace sd {

const float kFmRatios[kFmRatioCount] = {1.0f, 0.5f, 3.5f};
const char* const kFmRatioNames[kFmRatioCount] = {"1:1 warm", "1:2 hollow",
                                                  "3.5 bell"};

namespace {

constexpr float kTwoPi = 6.28318530717958647692f;

/// Where the envelope is quiet enough to stop rendering. Below this the samples
/// are inaudible and every one costs buffer.
constexpr float kEnvFloor = 0.02f;

}  // namespace

float fmSafeIndex(float carrierHz, float ratio, float sampleHz) {
    if (carrierHz <= 0.0f || ratio <= 0.0f || sampleHz <= 0.0f) return 0.0f;

    const float nyquist = sampleHz * 0.5f;
    // Carson: occupied bandwidth is about 2*(I + 1)*fm centred on the carrier,
    // so the upper edge sits at carrier + (I + 1)*ratio*carrier. Solve for I
    // against Nyquist, with a little margin.
    const float modHz = ratio * carrierHz;
    const float room  = nyquist * 0.9f - carrierHz;
    if (room <= 0.0f) return 0.0f;

    const float index = room / modHz - 1.0f;
    return (index > 0.0f) ? index : 0.0f;
}

size_t fmRenderNote(int16_t* out, size_t capacity, float carrierHz, float ratio,
                    float index, float decaySec, float amplitude,
                    float sampleHz) {
    if (!out || capacity == 0 || carrierHz <= 0.0f || sampleHz <= 0.0f) return 0;
    if (decaySec <= 0.0f) return 0;

    if (amplitude < 0.0f) amplitude = 0.0f;
    if (amplitude > 1.0f) amplitude = 1.0f;

    // A caller cannot ask for aliasing: whatever brightness was requested is
    // capped at what this carrier can carry.
    const float safe = fmSafeIndex(carrierHz, ratio, sampleHz);
    if (index < 0.0f) index = 0.0f;
    if (index > safe) index = safe;

    // Exponential decay reaching the audible floor exactly at decaySec, so the
    // control means what it says rather than being a time constant the player
    // has to learn.
    const float decayPerSample = std::pow(kEnvFloor, 1.0f / (decaySec * sampleHz));

    const float carrierStep = kTwoPi * carrierHz / sampleHz;
    const float modStep     = kTwoPi * carrierHz * ratio / sampleHz;

    float env       = 1.0f;
    float carPhase  = 0.0f;
    float modPhase  = 0.0f;
    const float peak = amplitude * 32000.0f;  // headroom below full scale

    size_t n = 0;
    for (; n < capacity && env > kEnvFloor; ++n) {
        // The index rides the envelope. A struck object is brightest at the
        // strike and dulls as it rings down; a constant index stays buzzy while
        // fading, which sounds synthetic in the bad way.
        const float mod = std::sin(modPhase) * index * env;
        out[n] = static_cast<int16_t>(std::sin(carPhase + mod) * env * peak);

        carPhase += carrierStep;
        modPhase += modStep;
        if (carPhase > kTwoPi) carPhase -= kTwoPi;
        if (modPhase > kTwoPi) modPhase -= kTwoPi;
        env *= decayPerSample;
    }
    return n;
}

}  // namespace sd
