# Writing an app

One file. No other file in the tree changes.

```cpp
// src/apps/RulerApp.cpp
#include "core/App.h"
#include "services/ServiceHub.h"
#include "ui/Widgets.h"

namespace sd {
namespace {

constexpr AppInfo kInfo{
    .id       = "ruler",              // stable; also the NVS namespace (<=15 chars)
    .title    = "RULER",              // shown in the launcher
    .subtitle = "on-screen scale",
    .group    = "MEASURE",            // launcher grouping / sort key
    .needs    = Cap::Display,         // acquired before onStart()
    .rotation = 1,                    // 0/2 portrait 135x240, 1/3 landscape 240x135
    .tickHz   = 30,
    .drawHz   = 30,
    .accent   = ui::theme::kCyan,

    // Its page in HELP. Three short strings; leave them out and the tool is
    // simply absent from HELP rather than listed with nothing under it.
    .what     = "An on-screen scale, in millimetres and inches.",
    .how      = "A switches units. B changes the zero.",
    .care     = "Calibrate against something known once: panels differ.",
};

class RulerApp : public App {
public:
    void onStart() override {}
    void onStop()  override {}

    bool onEvent(const InputEvent& ev) override {
        if (ev.click(Btn::A)) { /* … */ return true; }
        return false;                 // let Back and Menu reach the shell
    }

    void onTick(uint32_t dtUs) override {}

    void onDraw(ui::Canvas& c) override {
        c.textAt(c.w() / 2, 60, "hello", ui::theme::kText,
                 &fonts::FreeSansBold12pt7b, middle_center);
    }

    const char* hintA() const override { return "A mark"; }
    const char* hintB() const override { return "B mode"; }
};

}  // namespace

SD_REGISTER_APP(RulerApp, kInfo)

}  // namespace sd
```

Build, flash, and it is in the launcher.

---

## Rules worth keeping

**Declare hardware, do not take it.** Put what you need in `AppInfo::needs`. Do
not call `M5.Speaker.begin()`, `M5.Power.setExtOutput()` or friends yourself —
`ServiceHub` owns the sequencing and the conflicts, and going around it is how
one app breaks another. If you need a peripheral that has no `Cap` bit yet, add
the bit and its rules to `ServiceHub`, not to your app.

**Do not consume `Back` unless you have a screen to go back to.** Returning
`false` from `onEvent` lets the shell close your app. Consume `EvType::Back`
only while a sub-screen of your own is open, and pop that sub-screen instead.

**Draw the whole frame, every frame.** The canvas is cleared for you. Do not try
to draw incrementally; there is no guarantee about what was there before.

**Keep state changes out of `onDraw`.** Advance state in `onTick` or `onEvent`.
A frame drawn half in one screen and half in the next is the bug you get
otherwise.

**Put non-obvious maths in `mathx/`** — no Arduino headers — and write host
tests for it. If the correctness of your app is not visually obvious, this is
the difference between a tool and a toy.

**Persist through `Settings`**, keyed by your `AppInfo::id`. Version your blobs
with a magic number so an old record is ignored rather than misread.

**Write your HELP page** — the `what`, the `how`, and the one thing in
`care` that will catch you out. There is no central list to add yourself to;
HELP renders whatever the registry holds. The panel gives you three lines, two
and three respectively, and `python3 tools/help_fit.py` does the arithmetic. It
is worth running: written without measuring, nineteen of the first twenty pages
overflowed and would have been clipped mid-sentence.

## Sub-screens

Most tools want more than one view. Use an enum and switch inside `onDraw` /
`onEvent`, as `SystemInfoApp` does with `Page::{Vitals, Thermal, Settings}`.
Reach for a second registered app only when the thing is genuinely a separate
tool that deserves its own launcher entry; `AppManager::launch()` keeps a
navigation stack six deep if you do.

## Tools inside tools

A tool that belongs inside another rather than in the launcher - a game in
GAMES - names its parent in its own `AppInfo`:

```cpp
    .parent = "games",
```

The launcher skips anything with a parent and the parent lists whatever names
it, so the new tool is still one file and GAMES is never edited to learn about
it. Everything else about it is unchanged: it declares its own hardware, writes
its own HELP page, keeps its own settings, and Back inside it returns to the
parent rather than home, because `AppManager` keeps a navigation stack.

## Timing

`tickHz` should exceed the rate of whatever you are sampling, so you never miss
a sample; `drawHz` should be whatever the eye needs, which is usually far less.
The level ticks at 200 Hz and draws at 25.

## Chrome

The shell draws the status bar and footer. Feed them with:

- `hintA()` / `hintB()` — the footer hints; update them with your state so the
  buttons are always self-describing.
- `badge()` / `badgeColour()` — a short status tag such as `REC`, `CAL`, `HOLD`.
- `apps().toast("saved", ui::theme::kOk)` — a transient message.

Set `AppInfo::fullscreen = true` to suppress both and own all 240×135.

## Adding a page to the console

A feature that has more to show than a 240×135 panel holds puts a page beside
its own code, and CONSOLE serves it without being edited:

```cpp
void ridesPage(Page& p) {
    p.open();                       // the document and the shared stylesheet
    p.out().put("<h1>RIDES</h1>");  // the heading is yours, so it can be dynamic
}

constexpr PageInfo kRidesPage{
    .path   = "/rides",
    .method = Method::Get,
    .title  = "RIDES",                          // the browser tab
    .nav    = "the rides it has recorded",      // its button on the front page
    .note   = kRidesNote,                       // what it says about itself
    .css    = kRidesCss,                        // rules only this page needs
    .fn     = ridesPage,
};

SD_REGISTER_PAGE(RidesPage, kRidesPage)
```

Deleting the file deletes the route, the button and the footer link together.
Four rules:

1. **Use the feature's own path for everything it owns** — `/notes`, then
   `/notes/add` and `/notes/del`. The root belongs to the base's front page,
   `web/IndexPage.cpp`, which the captive portal redirects every request to.
2. **`nav` is a place to go, not a thing to do.** A POST target, a CSV or a PNG
   leaves it null and is reached from the page above it.
3. **Reach the data through a service, never into the app.** An app writes to a
   service from one side and the page reads it from the other; one app runs at a time, so a
   page cannot ask the app anything.
4. **`css` is for what one page alone wants.** A rule two pages want belongs in
   the frame, in `web/Console.cpp`, where it cannot fork — there were two
   stylesheets once and they had drifted.

Write nothing long into `putf()`: its scratch line is 512 bytes and it truncates
in silence. Long prose is `put()`, and anything that came from outside the page
goes through `escaped()`.
