#pragma once
// -----------------------------------------------------------------------------
//  InputService - the buttons, read.
//
//  Built on M5Unified's debounced button state, not raw GPIO. Every decision,
//  click, hold, repeat and the A+B chord, is mathx/ButtonGrammar.h, host-tested
//  against the finger sequences that used to go wrong; this file samples the
//  hardware and hands the samples over.
//
//  Events are queued rather than dispatched from the poll, so an app can be
//  torn down mid-gesture without an event landing on a destroyed object.
// -----------------------------------------------------------------------------
#include "core/Event.h"
#include "mathx/ButtonGrammar.h"

#include <cstdint>

namespace sd {

class InputService {
public:
    static constexpr uint32_t kLongMs        = ButtonGrammar::kLongMs;
    static constexpr uint32_t kRepeatFirstMs = ButtonGrammar::kRepeatFirstMs;
    static constexpr uint32_t kRepeatMs      = ButtonGrammar::kRepeatMs;
    static constexpr uint32_t kDoubleMs      = ButtonGrammar::kDoubleMs;

    void begin();

    /// Reads button state and fills the queue. Call once per loop, after
    /// M5.update().
    void poll(uint32_t nowMs);

    /// Pops the next event, or an event with type None when the queue is empty.
    InputEvent next() { return _grammar.next(); }

    void flush() { _grammar.flush(); }

    /// The raw state of one button, right now. For an app that needs the moment
    /// a finger lands rather than the gesture it turns out to be: a clutch has
    /// to engage on the press, and Click and LongPress both arrive later.
    bool down(Btn b) const { return _grammar.down(b); }

private:
    ButtonGrammar _grammar;
};

}  // namespace sd
