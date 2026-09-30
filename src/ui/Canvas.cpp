#include "ui/Canvas.h"

#include "core/Log.h"

#include <cstdarg>
#include <cstdio>

namespace sd::ui {

bool Canvas::allocate(int w, int h) {
    setColorDepth(16);

    setPsram(false);
    if (createSprite(w, h) == nullptr) {
        SD_LOGW("canvas", "%dx%d did not fit in SRAM, falling back to PSRAM", w, h);
        setPsram(true);
        if (createSprite(w, h) == nullptr) {
            SD_LOGE("canvas", "failed to allocate %dx%d frame buffer", w, h);
            // deleteSprite() has already run, so the old buffer is gone. Say
            // so: AppManager skips the frame on w() == 0, and leaving the
            // previous size here had it drawing into a freed sprite.
            _w = 0;
            _h = 0;
            return false;
        }
    }
    _w = w;
    _h = h;
    setTextWrap(false);
    fillSprite(theme::kBg);
    return true;
}

void Canvas::textAt(int x, int y, const char* s, uint16_t colour,
                    const lgfx::IFont* font, textdatum_t datum) {
    setFont(font);
    setTextColor(colour);
    setTextDatum(datum);

#if defined(SD_DEBUG)
    // A string wider than the panel is clipped silently and reads as a sentence
    // that stops mid-word. That is a bug report from a user rather than
    // something the build tells you, so the debug build tells you instead.
    const int tw = textWidth(s);
    if (tw > _w)
        SD_LOGW("canvas", "text %d px wide on a %d px panel: \"%s\"", tw, _w, s);
#endif

    drawString(s, x, y);
}

void Canvas::textf(int x, int y, uint16_t colour, const lgfx::IFont* font,
                   textdatum_t datum, const char* fmt, ...) {
    char    buf[96];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    textAt(x, y, buf, colour, font, datum);
}

void Canvas::degreeMark(int x, int y, int radius, uint16_t colour) {
    drawCircle(x, y, radius, colour);
    if (radius > 3) drawCircle(x, y, radius - 1, colour);
}

void Canvas::rule(int y, uint16_t colour, int pad) {
    drawFastHLine(pad, y, _w - pad * 2, colour);
}

}  // namespace sd::ui
