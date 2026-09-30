#pragma once
// -----------------------------------------------------------------------------
//  Shared chrome. Every app gets the same status bar and footer for free, which
//  is what makes a device full of unrelated tools feel like one instrument.
// -----------------------------------------------------------------------------
#include "ui/Canvas.h"

namespace sd::ui {

struct StatusInfo {
    const char* title      = "";
    const char* badge      = nullptr;  ///< small right-aligned tag, e.g. "CAL"
    uint16_t    badgeColour = theme::kAccent;
    int         batteryPct = -1;       ///< <0 hides the gauge
    bool        charging   = false;
    /// Library Mode is on. Drawn as its own tag beside the battery rather than
    /// through `badge`, which stays the running app's to use.
    bool        libraryMode = false;
    uint16_t    accent     = theme::kAccent;
};

/// Status bar across the top. Returns the y of the first free pixel row.
int drawStatusBar(Canvas& c, const StatusInfo& s);

/// Button hints along the bottom. Returns the y of the last free pixel row.
int drawFooter(Canvas& c, const char* hintA, const char* hintB);

/// A short-lived message strip, drawn over whatever is underneath.
void drawToast(Canvas& c, const char* msg, uint16_t colour);

/// Draws `text` word-wrapped into `w` pixels from (x, y), a line every
/// `lineH`, and returns the y after the last one.
///
/// Stops rather than drawing past `maxY`, and returns a y beyond it when it had
/// to, so a caller that cares whether everything fitted can tell. That is how
/// HELP notices a page somebody wrote too much for.
///
/// A newline is a hard break and an empty line is a blank one. A word wider
/// than the whole line is broken where it stops fitting, which an address
/// pasted into a note needs: it has no space to break at.
///
/// `stoppedAt`, when given, receives where in `text` it got to: the end on a
/// complete draw, otherwise the start of the first line it could NOT fit.
/// Passing that back as the next call's `text` pages through a long note with
/// no line left out and none drawn twice.
int drawWrapped(Canvas& c, int x, int y, int w, int maxY, int lineH,
                const char* text, uint16_t colour, const lgfx::IFont* font,
                const char** stoppedAt = nullptr);

// -----------------------------------------------------------------------------
//  ListMenu - the scrolling selector used by the launcher and by any app that
//  needs to pick from a handful of options.
// -----------------------------------------------------------------------------
struct MenuItem {
    const char* title    = "";
    const char* subtitle = nullptr;
    uint16_t    accent   = 0;  ///< 0 = inherit
};

class ListMenu {
public:
    void setCount(int n);
    int  count() const { return _count; }

    void next();
    void prev();
    int  selected() const { return _sel; }
    void select(int i);

    /// `provider` fills in the item for a given index, which keeps the menu
    /// free of storage of its own so it can front a registry, a file list or a
    /// fixed array without copying.
    using Provider = void (*)(int index, MenuItem& out, void* ctx);

    /// `titleFont` is settable because a list of notes is a list of somebody's
    /// own words, and Font2 stops at 0x7E, so a Turkish title in it is a row of
    /// boxes. The subtitle stays small and is not settable: a 16 px font on the
    /// second line of a 26 px row overlaps both the title and the row below.
    void draw(Canvas& c, int top, int bottom, Provider provider, void* ctx,
              uint16_t accent, const lgfx::IFont* titleFont = &fonts::Font2);

private:
    int _count = 0;
    int _sel   = 0;
    int _top   = 0;  ///< first visible row
};

}  // namespace sd::ui
