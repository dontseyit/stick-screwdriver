#include "ui/Widgets.h"

#include <cstdio>
#include <cstring>

namespace sd::ui {

namespace {
constexpr int kRowH = 26;

/// A small filled tag in the status bar, drawn right-to-left from `right`,
/// which it moves on past itself so the next one lands beside it.
void drawChip(Canvas& c, int h, int& right, const char* text, uint16_t bg) {
    c.setFont(&fonts::Font0);
    const int tw = c.textWidth(text);
    c.fillRoundRect(right - tw - 5, 2, tw + 6, h - 4, 2, bg);
    c.textAt(right - tw - 2, h / 2, text, theme::kBg, &fonts::Font0, middle_left);
    right -= tw + 9;
}
}

int drawStatusBar(Canvas& c, const StatusInfo& s) {
    const int h = theme::kBarH;
    c.fillRect(0, 0, c.w(), h, theme::kSurface);
    c.drawFastHLine(0, h, c.w(), s.accent);

    c.textAt(4, h / 2, s.title, s.accent, &fonts::Font0, middle_left);

    int right = c.w() - 3;

    if (s.batteryPct >= 0) {
        // 18x8 cell with a nub, filled proportionally.
        const int bw = 18, bh = 8;
        const int bx = right - bw, by = (h - bh) / 2;
        const uint16_t col = s.charging      ? theme::kOk
                             : (s.batteryPct < 15) ? theme::kErr
                             : (s.batteryPct < 35) ? theme::kWarn
                                                   : theme::kDim;
        c.drawRect(bx, by, bw, bh, col);
        c.drawFastVLine(bx + bw, by + 2, bh - 4, col);
        const int fill = (bw - 4) * s.batteryPct / 100;
        if (fill > 0) c.fillRect(bx + 2, by + 2, fill, bh - 4, col);
        right = bx - 5;
    }

    // Before the app's badge, so the one thing true on every screen sits in the
    // same place on every screen.
    if (s.libraryMode) drawChip(c, h, right, "LIB", theme::kWarn);
    if (s.badge)       drawChip(c, h, right, s.badge, s.badgeColour);

    return h + 1;
}

int drawFooter(Canvas& c, const char* hintA, const char* hintB) {
    if (!hintA && !hintB) return c.h() - 1;

    const int h = theme::kFootH;
    const int y = c.h() - h;
    c.drawFastHLine(0, y, c.w(), theme::kLine);
    if (hintA) c.textAt(4, y + h / 2 + 1, hintA, theme::kDim, &fonts::Font0, middle_left);
    if (hintB) c.textAt(c.w() - 4, y + h / 2 + 1, hintB, theme::kDim, &fonts::Font0, middle_right);
    return y - 1;
}

void drawToast(Canvas& c, const char* msg, uint16_t colour) {
    c.setFont(&fonts::Font0);
    const int tw = c.textWidth(msg);
    const int bw = tw + 16;
    const int bh = 16;
    const int x  = (c.w() - bw) / 2;
    const int y  = c.h() - theme::kFootH - bh - 6;
    c.fillRoundRect(x, y, bw, bh, 3, colour);
    c.textAt(c.w() / 2, y + bh / 2, msg, theme::kBg, &fonts::Font0, middle_center);
}

// -----------------------------------------------------------------------------
void ListMenu::setCount(int n) {
    _count = n < 0 ? 0 : n;
    if (_sel >= _count) _sel = _count > 0 ? _count - 1 : 0;
}

void ListMenu::next() {
    if (_count > 0) _sel = (_sel + 1) % _count;
}

void ListMenu::prev() {
    if (_count > 0) _sel = (_sel + _count - 1) % _count;
}

void ListMenu::select(int i) {
    if (_count > 0) _sel = (i % _count + _count) % _count;
}

void ListMenu::draw(Canvas& c, int top, int bottom, Provider provider, void* ctx,
                    uint16_t accent, const lgfx::IFont* titleFont) {
    if (_count == 0 || provider == nullptr) {
        c.textAt(c.w() / 2, (top + bottom) / 2, "no items", theme::kDim,
                 &fonts::Font0, middle_center);
        return;
    }

    const int visible = (bottom - top) / kRowH;
    if (visible <= 0) return;

    // Keep the selection inside the window without recentring on every step:
    // the list only scrolls when the cursor would otherwise leave the screen.
    if (_sel < _top) _top = _sel;
    if (_sel >= _top + visible) _top = _sel - visible + 1;
    if (_top > _count - visible) _top = _count - visible;
    if (_top < 0) _top = 0;

    for (int row = 0; row < visible && (_top + row) < _count; ++row) {
        const int idx = _top + row;
        const int y   = top + row * kRowH;

        MenuItem item;
        provider(idx, item, ctx);
        const uint16_t itemAccent = item.accent ? item.accent : accent;
        const bool     sel        = (idx == _sel);

        if (sel) {
            c.fillRoundRect(2, y + 1, c.w() - 4, kRowH - 2, 3, theme::kSurface);
            c.fillRect(2, y + 1, 3, kRowH - 2, itemAccent);
        }
        c.textAt(10, y + (item.subtitle ? 9 : kRowH / 2), item.title,
                 sel ? theme::kText : theme::kDim, titleFont, middle_left);
        if (item.subtitle) {
            c.textAt(10, y + 19, item.subtitle, sel ? itemAccent : theme::kFaint,
                     &fonts::Font0, middle_left);
        }
    }

    // Scroll indicator, only when it means something.
    if (_count > visible) {
        const int trackH = bottom - top - 4;
        const int barH   = trackH * visible / _count;
        const int barY   = top + 2 + trackH * _top / _count;
        c.fillRect(c.w() - 3, top + 2, 1, trackH, theme::kLine);
        c.fillRect(c.w() - 4, barY, 3, barH < 6 ? 6 : barH, accent);
    }
}

// -----------------------------------------------------------------------------
namespace {
/// `n` backed off until it does not fall inside a UTF-8 sequence.
int wholeChars(const char* s, int n) {
    while (n > 0 && (static_cast<unsigned char>(s[n]) & 0xC0) == 0x80) --n;
    return n;
}
}  // namespace

int drawWrapped(Canvas& c, int x, int y, int w, int maxY, int lineH,
                const char* text, uint16_t colour, const lgfx::IFont* font,
                const char** stoppedAt) {
    if (stoppedAt) *stoppedAt = text;
    if (!text || !*text) return y;
    c.setFont(font);

    char line[96];
    char cand[96];
    int  len = 0;
    line[0] = '\0';

    const char* p = text;
    // Where the text currently held in `line` began. Everything before it has
    // been drawn and everything from it has not, which is what a caller
    // resuming on the next page needs to be handed.
    const char* lineStart = text;

    while (true) {
        // Spaces only. A newline is a break rather than a separator, and
        // swallowing it here is what used to send it to the font as a glyph.
        while (*p == ' ') ++p;

        if (*p == '\n') {
            if (y + lineH > maxY) {
                if (stoppedAt) *stoppedAt = lineStart;
                return y + lineH;
            }
            // Drawn even when empty, which costs nothing and is how a blank
            // line between paragraphs survives.
            c.textAt(x, y, line, colour, font, top_left);
            y += lineH;
            len     = 0;
            line[0] = '\0';
            ++p;
            lineStart = p;
            continue;
        }
        if (!*p) break;

        const char* word = p;
        while (*p && *p != ' ' && *p != '\n') ++p;
        int wlen = static_cast<int>(p - word);
        if (wlen > static_cast<int>(sizeof(line)) - 1) {
            wlen = sizeof(line) - 1;
            wlen = wholeChars(word, wlen);
        }

        // The line buffer filling before the panel does needs 96 characters on
        // a 240 pixel panel, so it does not happen today. The flush still has
        // to come before the separator is decided, or the fresh line starts
        // with the space that was going to join it to the previous word.
        if (len + 1 + wlen >= static_cast<int>(sizeof(cand))) {
            if (y + lineH > maxY) {
                if (stoppedAt) *stoppedAt = lineStart;
                return y + lineH;
            }
            c.textAt(x, y, line, colour, font, top_left);
            y += lineH;
            len = 0;
        }
        if (len == 0) lineStart = word;

        // A single word wider than the whole line: a pasted URL, a long code,
        // the sort of thing that turns up in a note. There is no space to break
        // it at, so it is broken where it stops fitting.
        //
        // Emitted as its own line straight away rather than left in the buffer:
        // the remainder becomes the next word, and joining it back on would put
        // a space in the middle of somebody's address.
        if (len == 0) {
            std::memcpy(cand, word, static_cast<size_t>(wlen));
            cand[wlen] = '\0';
            if (c.textWidth(cand) > w) {
                int fit = wlen - 1;
                while (fit > 1) {
                    cand[fit] = '\0';
                    if (c.textWidth(cand) <= w) break;
                    --fit;
                }
                // Never between the bytes of one character: half a g-breve is
                // not a narrower letter, it is a glyph the font has no drawing
                // for and a byte the next line starts with.
                fit = wholeChars(word, fit);
                if (fit < 1) fit = 1;
                cand[fit] = '\0';
                if (y + lineH > maxY) {
                    if (stoppedAt) *stoppedAt = lineStart;
                    return y + lineH;
                }
                c.textAt(x, y, cand, colour, font, top_left);
                y += lineH;
                p         = word + fit;
                lineStart = p;
                continue;
            }
        }

        const int sep = (len > 0) ? 1 : 0;
        std::memcpy(cand, line, static_cast<size_t>(len));
        if (sep) cand[len] = ' ';
        std::memcpy(cand + len + sep, word, static_cast<size_t>(wlen));
        cand[len + sep + wlen] = '\0';

        if (len > 0 && c.textWidth(cand) > w) {
            if (y + lineH > maxY) {
                if (stoppedAt) *stoppedAt = lineStart;
                return y + lineH;
            }
            c.textAt(x, y, line, colour, font, top_left);
            y += lineH;
            std::memcpy(line, word, static_cast<size_t>(wlen));
            line[wlen] = '\0';
            len        = wlen;
            lineStart  = word;
        } else {
            len = len + sep + wlen;
            std::memcpy(line, cand, static_cast<size_t>(len) + 1);
        }
    }

    if (len > 0) {
        if (y + lineH > maxY) {
            if (stoppedAt) *stoppedAt = lineStart;
            return y + lineH;
        }
        c.textAt(x, y, line, colour, font, top_left);
        y += lineH;
    }
    if (stoppedAt) *stoppedAt = p;
    return y;
}

}  // namespace sd::ui
