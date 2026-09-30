#include "mathx/ButtonGrammar.h"

namespace sd {

void ButtonGrammar::flush() {
    _head = _tail = 0;
    for (auto& b : _btn) b = BtnState{};
    _bothDown = false;
}

void ButtonGrammar::push(const InputEvent& ev) {
    const int nxt = (_head + 1) % kQueue;
    if (nxt == _tail) return;  // full: drop the newest rather than stall
    _q[_head] = ev;
    _head     = nxt;
}

InputEvent ButtonGrammar::next() {
    if (_tail == _head) return {};
    const InputEvent ev = _q[_tail];
    _tail               = (_tail + 1) % kQueue;
    return ev;
}

void ButtonGrammar::pollButton(Btn id, bool pressed, uint32_t now) {
    BtnState& s = _btn[static_cast<int>(id)];

    if (pressed && !s.down) {
        s.down       = true;
        s.downAt     = now;
        s.longFired  = false;
        s.suppressed = false;
        s.repeats    = 0;
        return;
    }

    if (pressed && s.down) {
        if (s.suppressed) return;
        const uint32_t held = now - s.downAt;
        if (!s.longFired && held >= kLongMs) {
            s.longFired  = true;
            s.lastRepeat = now;
            // B held is the universal way back out of an app.
            push(InputEvent{id == Btn::B ? EvType::Back : EvType::LongPress, id, held, 0});
        } else if (s.longFired) {
            const uint32_t gap = (s.repeats == 0) ? kRepeatFirstMs : kRepeatMs;
            if (now - s.lastRepeat >= gap) {
                s.lastRepeat = now;
                push(InputEvent{EvType::Repeat, id, held, ++s.repeats});
            }
        }
        return;
    }

    if (!pressed && s.down) {
        const uint32_t held = now - s.downAt;
        s.down              = false;
        if (s.suppressed) return;
        push(InputEvent{EvType::Release, id, held, s.repeats});
        if (!s.longFired) {
            push(InputEvent{EvType::Click, id, held, 0});
            if (s.lastClickAt != 0 && (now - s.lastClickAt) <= kDoubleMs) {
                push(InputEvent{EvType::DoubleClick, id, held, 0});
                s.lastClickAt = 0;
            } else {
                s.lastClickAt = now;
            }
        }
    }
}

void ButtonGrammar::poll(bool a, bool b, bool pwr, uint32_t now) {
    // ---- A+B chord -> MENU -------------------------------------------------
    // Decided BEFORE the buttons are polled, on the sample where the second
    // finger lands. A press transition emits nothing by itself and a long press
    // has a 550 ms fuse, so at this instant nothing from either button has
    // escaped unless one has already gone long, which is the one case that is
    // not a chord.
    BtnState& sa = _btn[static_cast<int>(Btn::A)];
    BtnState& sb = _btn[static_cast<int>(Btn::B)];
    if (a && b) {
        if (!_bothDown) {
            _bothDown = true;
            if (!sa.longFired && !sb.longFired) {
                sa.suppressed = true;
                sb.suppressed = true;
                // The button already down says how long the chord took to form;
                // the other has no downAt yet.
                const uint32_t first = sa.down ? sa.downAt : (sb.down ? sb.downAt : now);
                push(InputEvent{EvType::Menu, Btn::A, now - first, 0});
            }
        }
    } else if (!a && !b) {
        _bothDown = false;
    }

    // Suppression has to be in place before the press transition is recorded,
    // or a button landing on this sample would clear it.
    const bool suppA = sa.suppressed, suppB = sb.suppressed;
    pollButton(Btn::A, a, now);
    pollButton(Btn::B, b, now);
    if (_bothDown) {
        sa.suppressed = suppA || sa.suppressed;
        sb.suppressed = suppB || sb.suppressed;
    }

    pollButton(Btn::Pwr, pwr, now);
}

}  // namespace sd
