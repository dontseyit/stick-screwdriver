#pragma once
// -----------------------------------------------------------------------------
//  AppManager - lifecycle, scheduling and the shared frame.
//
//  Owns the one canvas, runs each app at its declared tick and draw rates, and
//  keeps a small navigation stack so an app can open a sub-app and get control
//  back. Transitions are deferred to the end of a loop iteration, so an app can
//  ask to be closed from inside its own event handler.
// -----------------------------------------------------------------------------
#include "core/App.h"
#include "ui/Canvas.h"

#include <cstdint>

namespace sd {

class AppManager {
public:
    static constexpr int kStackDepth = 6;

    static AppManager& instance();

    /// `homeId` is the app shown at boot and returned to by Back at depth 1.
    bool begin(const char* homeId);

    /// Runs one iteration. Call from Arduino loop().
    void loop();

    /// Queues a switch to `id`. Safe to call from an app callback.
    bool launch(const char* id);
    /// Queues a return to the previous app, or to home.
    void back();

    ui::Canvas& canvas() { return _canvas; }
    App*        current() { return _app; }

    /// Flash a short message over the current app.
    void toast(const char* msg, uint16_t colour = 0, uint32_t ms = 1200);

    /// Claim hardware beyond what the running app declared, for a capability it
    /// only sometimes needs. Passing 0 drops back to the declared set, which is
    /// restored when the next app starts, so a forgotten release cannot leak a
    /// peripheral into another app.
    void requestCaps(CapMask extra);

    /// Turn the panel, for an app that reorients itself to how it is held. Both
    /// of these are presentation overrides rather than settings: the next app's
    /// declared values are restored when it starts.
    ///
    /// Call from onTick, not onDraw. The frame buffer is freed and reallocated,
    /// and doing that mid-frame would push a buffer of the wrong shape.
    void setRotation(uint8_t rotation);

    /// Hide the status bar, the footer and any toast, for a view meant to be
    /// looked at from across a room rather than read.
    void setChrome(bool on);
    bool chrome() const { return _chrome; }

    /// Stop pushing frames to the panel for `ms`. The screen holds its last
    /// content and the SPI bus goes quiet, which is the only way to test
    /// whether display traffic couples into audio, an analogue input or a
    /// radio. The app keeps ticking.
    void suspendDrawing(uint32_t ms);
    bool drawingSuspended() const;

private:
    AppManager() = default;

    void   startApp(const AppEntry& e);
    void   stopApp();
    void   applyRotation(uint8_t rotation);
    void   dispatchEvents();
    void   render(uint32_t nowMs);
    void   processPending();

    /// Turns the panel down, then off, when nothing has happened for a while.
    /// The app underneath goes on ticking, recording and serving.
    void   updateBacklight();
    void   wakeBacklight();

    ui::Canvas      _canvas;
    App*            _app        = nullptr;
    const char*     _homeId     = nullptr;

    const char* _stack[kStackDepth]{};
    int         _depth = 0;

    const char* _pendingLaunch = nullptr;
    bool        _pendingBack   = false;

    uint8_t  _rotation  = 0xFF;
    bool     _chrome    = true;
    uint32_t _lastTickUs = 0;
    uint32_t _lastDrawMs = 0;
    uint32_t _drawSuspendUntil = 0;
    uint32_t _tickAccUs  = 0;

    char     _toast[40]{};
    uint16_t _toastColour = 0;
    uint32_t _toastUntil  = 0;

    enum class Backlight : uint8_t { Awake, Dimmed, Blank };
    Backlight _backlight  = Backlight::Awake;
    /// What the app had the brightness at before dimming started, so a wake
    /// restores that rather than a framework constant. Zero until the first
    /// sample, and nothing dims before then.
    uint8_t  _wakeBright  = 0;
};

inline AppManager& apps() { return AppManager::instance(); }

}  // namespace sd
