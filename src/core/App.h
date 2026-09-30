#pragma once
// -----------------------------------------------------------------------------
//  The app contract.
//
//  Everything the device does is an App, including the launcher. An app is
//  described by a static AppInfo and built on demand by a factory, so an app
//  that is not running costs nothing but its AppInfo.
//
//  Adding an app is one .cpp file ending in SD_REGISTER_APP(...). There is no
//  central list to edit, so a new app cannot break an existing one.
// -----------------------------------------------------------------------------
#include "core/Caps.h"
#include "core/Event.h"

#include <cstdint>

namespace sd {

namespace ui { class Canvas; }

// -----------------------------------------------------------------------------
struct AppInfo {
    const char* id       = "";       ///< stable identifier; also the NVS namespace
    const char* title    = "";       ///< shown in the launcher
    const char* subtitle = "";       ///< one-line description
    const char* group    = "MISC";   ///< kind of tool; shown by HELP, not sorted on
    /// Sorts to the top of the launcher whatever its title, for the one app you
    /// should not have to find alphabetically.
    bool        pinned   = false;
    /// The app that lists this one instead of the launcher: a game names GAMES.
    /// The launcher skips anything with a parent and the parent lists whatever
    /// names it, so a nested tool is still one file. It stays a registered app
    /// in every other way, and Back inside it returns to the parent.
    const char* parent   = nullptr;
    CapMask     needs    = 0;        ///< hardware acquired before onStart()
    uint8_t     rotation = 1;        ///< 0/2 = portrait 135x240, 1/3 = landscape
    uint16_t    tickHz   = 30;       ///< desired onTick() rate
    uint16_t    drawHz   = 30;       ///< desired onDraw() rate
    /// Core clock while this app runs, MHz. 240 is the default, so declaring
    /// nothing changes nothing.
    ///
    /// A tool that draws a list and waits for a press can say 160 and idle at
    /// 27.6 mA instead of 32.9. APB is a fixed 80 MHz on the S3 whatever this
    /// is, so display SPI, the RMT that clocks IR, and I2S do not move with it.
    /// Compute-bound work gains nothing.
    uint16_t    cpuMhz   = 240;
    /// Seconds without a press or a pick-up before the panel dims; 45 is the
    /// default. A tool that is carried and listened to rather than watched can
    /// say less, since the panel is the largest load on the cell. Blanking
    /// stays at the framework's three minutes.
    uint16_t    dimAfterSec = 45;
    uint16_t    accent   = 0;        ///< RGB565 accent, 0 = use the theme default
    bool        fullscreen = false;  ///< suppress the framework status bar/footer

    // -- for the HELP app ----------------------------------------------------
    /// Three short strings. They live here rather than in a table inside HELP
    /// for the same reason as everything else about an app: a central list
    /// would need editing for every new tool. HELP renders what is registered.
    ///
    /// Keep them short. The panel is 240x135 and a page holds about eight
    /// lines; HELP warns in a debug build when one does not fit.
    const char* what = nullptr;  ///< what the tool is, in a sentence or two
    const char* how  = nullptr;  ///< how it is driven
    const char* care = nullptr;  ///< the thing that will catch you out
};

class App;
using AppFactory = App* (*)();

// -----------------------------------------------------------------------------
class App {
public:
    virtual ~App() = default;

    /// Called once after the app's capabilities have been acquired.
    virtual void onStart() {}
    /// Called once before its capabilities are released. Persist state here.
    virtual void onStop() {}

    /// One input gesture. Return true to consume it; unconsumed Back events
    /// close the app.
    virtual bool onEvent(const InputEvent& ev) { (void)ev; return false; }

    /// Fixed-rate logic, `dtUs` microseconds since the previous tick.
    virtual void onTick(uint32_t dtUs) { (void)dtUs; }

    /// Render a whole frame. The canvas is already cleared and is pushed by the
    /// framework, so apps never see a torn frame.
    virtual void onDraw(ui::Canvas& c) { (void)c; }

    /// Optional per-app hint line drawn by the framework in the footer.
    virtual const char* hintA() const { return nullptr; }
    virtual const char* hintB() const { return nullptr; }

    /// Optional short tag in the status bar, e.g. "CAL" or "REC".
    virtual const char* badge() const { return nullptr; }
    virtual uint16_t    badgeColour() const { return 0; }

    const AppInfo& info() const { return *_info; }

private:
    friend class AppManager;
    const AppInfo* _info = nullptr;
};

// -----------------------------------------------------------------------------
//  Registration
// -----------------------------------------------------------------------------
struct AppEntry {
    const AppInfo* info    = nullptr;
    AppFactory     factory = nullptr;
};

/// Constructing one of these at namespace scope adds the app to the registry.
struct AppRegistrar {
    AppRegistrar(const AppInfo& info, AppFactory factory);
};

/// Place at the bottom of an app's .cpp, next to its `kInfo`.
#define SD_REGISTER_APP(Class, INFO)                                      \
    static ::sd::App* Class##_factory() { return new Class(); }           \
    static const ::sd::AppRegistrar Class##_registrar{(INFO),             \
                                                      Class##_factory};

}  // namespace sd
