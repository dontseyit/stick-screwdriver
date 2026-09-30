#include "services/AudioService.h"

#include "core/Log.h"

#include "mathx/Fm.h"

#include <esp_heap_caps.h>

#include <M5Unified.h>

#include <cmath>

namespace sd {

static_assert(AudioService::kMaxVolume < 191,
              "board notes tie >=75% volume to brown-out reboots on battery");

bool AudioService::begin() {
    if (_on) return true;

    if (!M5.Speaker.isEnabled()) {
        // Only reachable if M5.begin() was never told the speaker exists, in
        // which case no pins were committed and there is nothing to start.
        SD_LOGE("audio", "speaker not configured - check cfg.internal_spk");
        return false;
    }

    _on = M5.Speaker.begin();
    if (_on) {
        _holdFreq = 0.0f;
        setVolumeRaw(_raw);
        SD_LOGI("audio", "speaker up, master volume %u", _raw);
    } else {
        SD_LOGE("audio", "speaker begin() failed");
    }
    return _on;
}

void AudioService::end() {
    if (!_on) return;
    M5.Speaker.stop();
    M5.Speaker.end();
    _on       = false;
    _holdFreq = 0.0f;
    SD_LOGI("audio", "speaker down");
}

void AudioService::forceOff() {
    // Speaker_Class::end() fires its disable callback before it checks whether
    // anything was ever started, so this reaches the PMIC either way.
    M5.Speaker.end();
    _on       = false;
    _holdFreq = 0.0f;
}

void AudioService::setVolumePercent(uint8_t pct) {
    if (pct > 100) pct = 100;
    const uint16_t span = kMaxVolume - kMinVolume;
    setVolumeRaw(
        static_cast<uint8_t>(kMinVolume + (span * static_cast<uint16_t>(pct)) / 100));
    _pct = pct;  // setVolumeRaw cannot know which percent produced it
}

void AudioService::setVolumeRaw(uint8_t v) {
    if (v > kSafeMaxVolume) v = kSafeMaxVolume;
    _raw = v;
    // Channel volume is left at its 255 default.
    if (_on) M5.Speaker.setVolume(v);
}

void AudioService::beep(float frequency, uint32_t durationMs) {
    if (!_on || frequency <= 0.0f || durationMs == 0) return;
    _holdFreq = 0.0f;
    M5.Speaker.tone(frequency, durationMs);
}

void AudioService::hold(float frequency) {
    if (!_on || frequency <= 0.0f) return;
    // Already holding this note: leave it alone. Restarting it would break a
    // tone whose whole job is to be steady.
    if (std::fabs(frequency - _holdFreq) < 0.5f) return;
    _holdFreq = frequency;
    M5.Speaker.tone(frequency, UINT32_MAX, kHoldChannel, true);
}

bool AudioService::playNote(float hz, float ratio, float brightness01,
                            float decaySec, float amplitude01) {
    if (!_on || hz <= 0.0f) return false;

    // Buffers are allocated on first use and kept for the life of the service.
    // They are large and PSRAM is idle; churning half a megabyte per note would
    // fragment the heap for no gain.
    if (_voiceLen == 0) {
        _voiceLen = fmNoteSamples(kMaxDecaySec) + 8;
        for (int i = 0; i < kVoices; ++i) {
            _voice[i] = static_cast<int16_t*>(
                heap_caps_malloc(_voiceLen * sizeof(int16_t), MALLOC_CAP_SPIRAM));
            if (!_voice[i]) {
                SD_LOGW("audio", "no PSRAM for voice %d", i);
                // Hand back what did fit. _voiceLen is the "not allocated yet"
                // flag, so leaving it zero with live pointers in _voice means
                // the next note re-enters this loop and overwrites them, and
                // that much PSRAM is gone until the next boot.
                for (int k = 0; k < i; ++k) {
                    heap_caps_free(_voice[k]);
                    _voice[k] = nullptr;
                }
                _voiceLen = 0;
                return false;
            }
        }
    }

    // A voice is free only when its channel has stopped reading its buffer.
    // playRaw keeps the pointer rather than a copy, so rendering into a buffer
    // still being played would corrupt the note in flight, which is why a busy
    // instrument drops notes instead of stealing voices.
    int slot = -1;
    for (int i = 0; i < kVoices; ++i) {
        if (M5.Speaker.isPlaying(static_cast<uint8_t>(kFirstVoice + i)) == 0) {
            slot = i;
            break;
        }
    }
    if (slot < 0) return false;

    if (decaySec > kMaxDecaySec) decaySec = kMaxDecaySec;
    const float safe  = fmSafeIndex(hz, ratio);
    const float index = brightness01 * safe;

    const size_t n = fmRenderNote(_voice[slot], _voiceLen, hz, ratio, index,
                                  decaySec, amplitude01);
    if (n == 0) return false;

    return M5.Speaker.playRaw(_voice[slot], n,
                              static_cast<uint32_t>(kSynthSampleHz), false, 1,
                              kFirstVoice + slot, false);
}

bool AudioService::playBuffer(const int16_t* pcm, size_t count, uint32_t rate) {
    if (!_on || !pcm || count == 0) return false;
    _holdFreq = 0.0f;
    // The reserved channel, and stop_current_sound true: a recording is the
    // whole output rather than one voice among several, so it takes the device
    // over rather than queueing.
    return M5.Speaker.playRaw(pcm, count, rate, false, 1, kHoldChannel, true);
}

bool AudioService::busy() const { return _on && M5.Speaker.isPlaying(); }

int AudioService::activeVoices() const {
    if (!_on) return 0;
    int n = 0;
    for (int i = 0; i < kVoices; ++i) {
        if (M5.Speaker.isPlaying(static_cast<uint8_t>(kFirstVoice + i))) ++n;
    }
    return n;
}

void AudioService::setExpression(uint8_t pct) {
    if (pct > 100) pct = 100;
    if (pct == _expr) return;
    _expr = pct;
    if (!_on) return;
    // Channel 0 is where tone() lands when no channel is named, which is how
    // hold() plays. Scaled to 255 rather than kSafeMaxVolume: this rides on top
    // of the master, so the board's power ceiling is already enforced by
    // whatever setVolumePercent left there.
    M5.Speaker.setChannelVolume(kHoldChannel,
                                static_cast<uint8_t>(pct * 255 / 100));
}

void AudioService::stop() {
    _holdFreq = 0.0f;
    if (_on) M5.Speaker.stop();
}

}  // namespace sd
