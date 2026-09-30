#pragma once
// -----------------------------------------------------------------------------
//  ButtonGrammar - two buttons, one grammar, identical in every app.
//
//  It is a timing state machine over three booleans and a clock, so it lives in
//  mathx where it can be tested. The M5 calls that feed it are in
//  services/InputService.h; everything with a decision in it is here.
//
//  The chord is decided on the way down. If both buttons are down at once and
//  neither has already committed to a long press, that gesture IS the chord:
//  Menu fires the moment the second finger lands, both buttons are silenced
//  until both are up, and nothing else comes out of the gesture. No Click on
//  the way up, no Back from the finger that lingers. The only way to press A+B
//  and not get Menu is to have already held one past the long-press threshold,
//  at which point the second finger starts a gesture of its own.
//
//  Deciding it on the way up instead, after 350 ms with the presses within
//  180 ms of each other, failed the way a hand actually presses a chord: the
//  first finger up emitted its Click and the one still down sailed on to Back.
//
//  Hardware-free; unit-tested on the host against the sequences that failed.
// -----------------------------------------------------------------------------
#include "core/Event.h"

#include <cstdint>

namespace sd {

class ButtonGrammar {
public:
    static constexpr uint32_t kLongMs        = 550;
    static constexpr uint32_t kRepeatFirstMs = 400;
    static constexpr uint32_t kRepeatMs      = 110;
    static constexpr uint32_t kDoubleMs      = 280;

    /// One sample of the three buttons. Call as often as you like; the grammar
    /// keeps its own clock from `nowMs`.
    void poll(bool a, bool b, bool pwr, uint32_t nowMs);

    /// Pops the next event, or one with type None when the queue is empty.
    InputEvent next();

    void flush();

    /// The raw state of one button, right now.
    bool down(Btn b) const { return _btn[static_cast<int>(b)].down; }

private:
    struct BtnState {
        bool     down        = false;
        uint32_t downAt      = 0;
        uint32_t lastRepeat  = 0;
        uint32_t lastClickAt = 0;
        bool     longFired   = false;
        bool     suppressed  = false;  ///< swallowed by a chord
        uint16_t repeats     = 0;
    };

    void push(const InputEvent& ev);
    void pollButton(Btn id, bool pressed, uint32_t now);

    static constexpr int kQueue = 12;
    InputEvent           _q[kQueue]{};
    int                  _head = 0, _tail = 0;

    BtnState _btn[static_cast<int>(Btn::Count)]{};
    /// Both buttons have been down together at some point in this gesture.
    /// Cleared only when both are up, so a chord is decided once.
    bool     _bothDown = false;
};

}  // namespace sd
