#pragma once
// -----------------------------------------------------------------------------
//  Canvas - the off-screen frame buffer every app draws into.
//
//  Apps never touch the panel directly. They are handed a cleared canvas, draw
//  a whole frame, and the framework pushes it in one go, so nothing tears or
//  flickers. Deriving from M5Canvas keeps the full LovyanGFX API available
//  alongside the helpers below.
// -----------------------------------------------------------------------------
#include <M5Unified.h>

#include "ui/Theme.h"

namespace sd::ui {

class Canvas : public M5Canvas {
public:
    Canvas() : M5Canvas(&M5.Display) {}

    /// Allocates the frame buffer. Internal SRAM first, because sprite pushes
    /// out of PSRAM are noticeably slower; PSRAM is the fallback.
    bool allocate(int w, int h);

    int  w() const { return _w; }
    int  h() const { return _h; }

    // -- text helpers, all taking the CENTRE or an anchor rather than the
    //    top-left, which is what layout code actually wants -----------------
    void textAt(int x, int y, const char* s, uint16_t colour,
                const lgfx::IFont* font, textdatum_t datum = middle_center);
    void textf(int x, int y, uint16_t colour, const lgfx::IFont* font,
               textdatum_t datum, const char* fmt, ...)
        __attribute__((format(printf, 7, 8)));

    /// Degree glyph drawn as a ring, so it can sit next to the seven-segment
    /// font, which has no such character.
    void degreeMark(int x, int y, int radius, uint16_t colour);

    /// Horizontal hairline at `y`, inset by `pad` on both sides.
    void rule(int y, uint16_t colour, int pad = 0);

private:
    int _w = 0, _h = 0;
};

}  // namespace sd::ui
