#pragma once
// -----------------------------------------------------------------------------
//  Input events.
//
//  The StickS3 gives two usable buttons plus the power key, so the grammar is
//  tight and identical in every app. The framework fixes the two universal
//  gestures and hands everything else to the focused app:
//
//    A  click / hold / repeat   primary action, app-defined
//    B  click                   secondary action, app-defined
//    B  hold                    BACK
//    A+B                        MENU, the moment both are down; nothing else
//                               comes out of it (mathx/ButtonGrammar.h)
//
//  Both are offered to the app first and handled centrally only if it declines,
//  so BACK always leads somewhere and no app can trap the user.
// -----------------------------------------------------------------------------
#include <cstdint>

namespace sd {

enum class Btn : uint8_t { A, B, Pwr, Count };

enum class EvType : uint8_t {
    None,
    Click,        ///< press and release inside the long-press window
    DoubleClick,
    LongPress,    ///< fired once, as soon as the hold threshold is crossed
    Repeat,       ///< fired periodically while a long press continues
    Release,
    Back,         ///< synthesised: leave the current app
    Menu,         ///< synthesised: open the app menu
};

struct InputEvent {
    EvType   type    = EvType::None;
    Btn      btn     = Btn::A;
    uint32_t heldMs  = 0;
    uint16_t repeats = 0;  ///< how many Repeats so far in this hold

    explicit operator bool() const { return type != EvType::None; }

    bool is(Btn b, EvType t) const { return btn == b && type == t; }
    bool click(Btn b) const        { return is(b, EvType::Click); }
    bool longPress(Btn b) const    { return is(b, EvType::LongPress); }
};

}  // namespace sd
