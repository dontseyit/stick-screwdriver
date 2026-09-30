#pragma once
// -----------------------------------------------------------------------------
//  AudioService - the speaker, wrapped.
//
//  A thin layer over the documented M5Unified Speaker_Class API (tone / stop /
//  setVolume). Synthesising waveforms with playRaw to beat tone()'s 16-sample
//  wavetable solved a problem the guidance tone no longer has: it runs between
//  1 and 4 kHz, and at 4 kHz on this board's 22050 Hz output there are about
//  five samples per cycle, so the output rate is the limit, not the table.
//  Below 1 kHz the wavetable would matter, but the speaker cannot usefully go
//  there anyway.
//
//  Two board facts are enforced here so no app has to remember them:
//
//  * StickS3 reboots if the speaker is driven hard on battery. The board notes
//    put that at 75% volume, 191 of 255, so nothing here goes near it.
//    M5Unified's own default is 64, which is what M5Stack's speaker example
//    runs at, so that is the reference point rather than the top of the dial.
//
//  * The ES8311 is only configured when M5.begin() was told the speaker exists.
//    begin()/end() here power the codec and amplifier up and down; the
//    configuration happens once at boot, in main.cpp.
// -----------------------------------------------------------------------------
#include <cstddef>
#include <cstdint>

namespace sd {

class AudioService {
public:
    /// Hard ceiling, below the 191 the board notes tie to brown-out reboots.
    static constexpr uint8_t kSafeMaxVolume = 180;
    /// Range the 0-100 percent scale spans, straddling M5Unified's default 64.
    static constexpr uint8_t kMinVolume = 40;
    static constexpr uint8_t kMaxVolume = 160;

    bool begin();
    void end();

    /// Drives the amplifier down whether or not this service believes it is up.
    ///
    /// The amplifier is a latched bit in the M5PM1 PMIC, register 0x11 bit 3,
    /// and the PMIC keeps its state across an ESP32 reset. So a session that
    /// played a single beep leaves the amplifier enabled for the NEXT boot, and
    /// a release path that only undoes what it acquired never clears it. IR
    /// reception does not work at all while it is on.
    void forceOff();
    bool available() const { return _on; }

    /// 0-100, scaled across kMinVolume..kMaxVolume.
    void    setVolumePercent(uint8_t pct);
    uint8_t volumePercent() const { return _pct; }

    /// Direct master volume, clamped to kSafeMaxVolume. For the audio test app,
    /// which needs the real knob rather than a friendly abstraction.
    void    setVolumeRaw(uint8_t v);
    uint8_t volumeRaw() const { return _raw; }

    /// Channels are named rather than left to the library. tone() with no
    /// channel picks the highest free one, so an expression write aimed at a
    /// fixed number would sometimes address a different note than the one
    /// playing, silently, and only when something else was sounding.
    static constexpr int kHoldChannel  = 0;
    static constexpr int kFirstVoice   = 1;
    /// M5Unified mixes eight channels and one is reserved above, so seven is
    /// the hardware ceiling rather than a chosen number. At top density against
    /// a long decay notes will be dropped, and the voice count on screen says
    /// so.
    static constexpr int kVoices       = 7;

    /// Longest note the voice buffers can hold. Each voice owns one, and
    /// playRaw keeps a pointer rather than a copy, so a buffer may not be
    /// touched again until its channel has finished with it.
    static constexpr float kMaxDecaySec = 1.2f;

    /// Render one FM note and play it on a free voice.
    ///
    /// Returns false when every voice is still ringing. Notes are dropped
    /// rather than stolen: reusing a buffer the I2S DMA is still reading
    /// corrupts the note that is playing, and a missing note is less noticeable
    /// than a burst of noise.
    bool playNote(float hz, float ratio, float brightness01, float decaySec,
                  float amplitude01);

    /// Voices currently ringing, for a display that shows what it is doing.
    int activeVoices() const;

    /// Plays a block of 16-bit mono PCM at `rate`.
    ///
    /// The buffer must outlive the playback and must not be written to while it
    /// runs: playRaw keeps a pointer rather than a copy. Anything already
    /// sounding is stopped, because this is a whole recording rather than one
    /// voice among several.
    bool playBuffer(const int16_t* pcm, size_t count, uint32_t rate);

    /// True while anything at all is coming out.
    bool busy() const;

    /// One beep of `durationMs`.
    void beep(float frequency, uint32_t durationMs);

    /// Continuous tone. Calling it again with the same frequency does nothing,
    /// so a held note is never restarted and never breaks up.
    void hold(float frequency);

    /// Level of a held note, 0-100, without disturbing it.
    ///
    /// Separate from setVolumePercent, which is the instrument's master
    /// setting. M5Unified keeps a per-channel volume alongside the master, and
    /// writing it does not restart the waveform, which is what makes a
    /// continuous swell possible. Changing the master would click on every
    /// update.
    void setExpression(uint8_t pct);

    void stop();

private:
    int16_t* _voice[kVoices]{};  ///< one PSRAM buffer each; see playNote()
    size_t   _voiceLen = 0;
    float    _holdFreq = 0.0f;
    uint8_t _expr     = 100;
    uint8_t _pct      = 60;
    uint8_t _raw      = 112;
    bool    _on       = false;
};

}  // namespace sd
