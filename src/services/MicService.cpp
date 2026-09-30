#include "services/MicService.h"

#include "core/Log.h"

#include <M5Unified.h>

namespace sd {

bool MicService::begin(uint32_t sampleRate, uint8_t magnification) {
    if (_on && _rate == sampleRate && _mag == magnification) return true;
    if (_on) end();

    if (!M5.Mic.isEnabled()) {
        // Only reachable if M5.begin() was never told the mic exists, in which
        // case no pins were committed and there is nothing to start.
        SD_LOGE("mic", "not configured - check cfg.internal_mic");
        return false;
    }
    _rate = sampleRate;
    _mag  = magnification;

    auto cfg          = M5.Mic.config();
    cfg.sample_rate   = _rate;
    cfg.magnification = _mag;
    cfg.over_sampling = 1;   // averaging is another hidden gain term
    cfg.stereo        = false;
    M5.Mic.config(cfg);

    _on = M5.Mic.begin();
    SD_LOGI("mic", _on ? "up at %u Hz, gain %u" : "begin() failed (%u Hz, gain %u)",
            static_cast<unsigned>(_rate), static_cast<unsigned>(_mag));
    return _on;
}

void MicService::end() {
    if (!_on) return;
    M5.Mic.end();
    _on = false;
    SD_LOGI("mic", "down");
}

bool MicService::queue(int16_t* buf, size_t samples) {
    if (!_on || !buf || samples == 0) return false;
    return M5.Mic.record(buf, samples, _rate);
}

size_t MicService::recording() const { return _on ? M5.Mic.isRecording() : 0; }

}  // namespace sd
