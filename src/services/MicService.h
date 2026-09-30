#pragma once
// -----------------------------------------------------------------------------
//  MicService - the ES8311's ADC side.
//
//  M5Unified's Mic_Class queues at most two buffers, so continuous capture
//  means keeping one queued while the other is examined. That double-buffering
//  is the only real subtlety, and it lives here rather than in every app.
// -----------------------------------------------------------------------------
#include <cstddef>
#include <cstdint>

namespace sd {

class MicService {
public:
    /// `magnification` is M5Unified's digital gain. Its default of 16 is
    /// roughly 4x on top of an ES8311 whose ADC is already at maximum gain,
    /// which clips anything loud: fine for a voice memo, useless for a
    /// measurement. Leave it at 1 unless you want loudness rather than a number.
    bool begin(uint32_t sampleRate, uint8_t magnification = 1);
    void end();
    bool available() const { return _on; }

    uint32_t sampleRate() const { return _rate; }
    /// Kept so ServiceHub can re-establish what an app configured rather than
    /// half of it. Restoring the rate and hardcoding the gain silently undid
    /// any app that wanted a different one.
    uint8_t  magnification() const { return _mag; }

    /// Queues a buffer to be filled. Returns false if the queue is full.
    bool queue(int16_t* buf, size_t samples);

    /// 0 = idle, 1 = recording with room for another, 2 = queue full.
    size_t recording() const;

private:
    uint32_t _rate = 16000;
    uint8_t  _mag  = 1;
    bool     _on   = false;
};

/// Keeps two blocks in flight and hands back whichever one filled.
///
/// The converter must always have somewhere to write or samples are lost, so
/// one block is being filled while the other is worked on. It was written out
/// twice, identically, in ROOMS and in CALIBRATION, which is the kind of
/// duplicate that fails quietly: a pump that loses track of which block is
/// which analyses real samples in the wrong order and reports a spectrum that
/// is merely plausible.
///
/// It owns no memory. The caller keeps the two blocks, because the caller
/// decides how big they are and where they live.
class MicPump {
public:
    /// Forgets what is in flight. Call it when the microphone stops, or the
    /// first block after restarting is credited to the wrong slot.
    void reset() { _write = 0; _queued = 0; }

    /// Queues whatever the microphone will take, then returns the index of a
    /// block that has finished filling, or -1 when none has yet.
    int ready(MicService& mic, int16_t* const block[2], size_t samples) {
        while (mic.recording() < 2 && _queued < 2) {
            if (!mic.queue(block[_write], samples)) break;
            _pending[_queued++] = _write;
            _write ^= 1;
        }
        // A block is done once the queue has drained below what we handed it.
        if (_queued == 0 || mic.recording() >= static_cast<size_t>(_queued)) return -1;

        const int done = _pending[0];
        _pending[0]    = _pending[1];
        --_queued;
        return done;
    }

private:
    int _write = 0;
    int _pending[2]{};
    int _queued = 0;
};

}  // namespace sd
