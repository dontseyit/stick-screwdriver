#include "core/AppManager.h"

#include "core/AppRegistry.h"
#include "core/Log.h"
#include "services/ServiceHub.h"
#include "services/SystemSettings.h"
#include "ui/Widgets.h"

#include <M5Unified.h>

#include <cstring>

namespace sd {

AppManager& AppManager::instance() {
    static AppManager mgr;
    return mgr;
}

// -----------------------------------------------------------------------------
void AppManager::applyRotation(uint8_t rotation) {
    if (rotation == _rotation) return;
    _rotation = rotation;
    M5.Display.setRotation(rotation);

    // Portrait and landscape need the same bytes, so freeing and reallocating
    // reuses the block instead of fragmenting the heap.
    _canvas.deleteSprite();
    if (!_canvas.allocate(M5.Display.width(), M5.Display.height())) {
        SD_LOGE("mgr", "frame buffer allocation failed");
    }
}

bool AppManager::begin(const char* homeId) {
    AppRegistry::instance().sort();
    _homeId = homeId;

    if (!AppRegistry::instance().find(homeId)) {
        SD_LOGE("mgr", "home app '%s' is not registered", homeId ? homeId : "(null)");
        return false;
    }
    _depth      = 0;
    _lastTickUs = micros();

    if (!launch(homeId)) return false;
    processPending();
    return _app != nullptr;
}

// -----------------------------------------------------------------------------
bool AppManager::launch(const char* id) {
    if (!AppRegistry::instance().find(id)) {
        SD_LOGW("mgr", "no such app '%s'", id ? id : "(null)");
        return false;
    }
    _pendingLaunch = id;
    _pendingBack   = false;
    return true;
}

void AppManager::back() {
    _pendingBack   = true;
    _pendingLaunch = nullptr;
}

void AppManager::startApp(const AppEntry& e) {
    // Before the capabilities, so the clock never moves under a radio this app
    // has just asked for. Asserted on every start, like rotation, so an app
    // that wanted more cannot leave it raised for whatever runs next.
    setCpuFrequencyMhz(e.info->cpuMhz);

    services().applyCaps(e.info->needs);

    _app = e.factory();
    if (!_app) {
        SD_LOGE("mgr", "factory for '%s' returned null", e.info->id);
        return;
    }
    _app->_info = e.info;

    applyRotation(e.info->rotation);
    // Presentation overrides belong to the app that asked for them, so anything
    // an outgoing app turned is turned back before the next one draws.
    // So does its last message: toasts run for a second or more and B-hold
    // fires at 550 ms, so an outgoing app's words would land on incoming frames.
    _toastUntil = 0;
    services().input().flush();

    _lastTickUs = micros();
    _tickAccUs  = 0;
    _lastDrawMs = 0;

    _app->onStart();
    services().mood().noteAppStart(e.info->id, e.info->title);
    SD_LOGI("mgr", "started '%s'", e.info->id);
}

void AppManager::stopApp() {
    if (!_app) return;
    SD_LOGI("mgr", "stopping '%s'", _app->info().id);
    services().mood().noteAppStop();
    _app->onStop();
    delete _app;
    _app = nullptr;
}

void AppManager::processPending() {
    if (_pendingBack) {
        _pendingBack = false;
        // Depth 1 is home; there is nowhere further back to go.
        if (_depth > 1) {
            --_depth;
            const AppEntry* e = AppRegistry::instance().find(_stack[_depth - 1]);
            if (e) {
                stopApp();
                startApp(*e);
            }
        } else if (_depth == 1 && std::strcmp(_stack[0], _homeId) != 0) {
            _pendingLaunch = _homeId;
            _depth         = 0;
        }
    }

    if (_pendingLaunch) {
        const char*     id = _pendingLaunch;
        const AppEntry* e  = AppRegistry::instance().find(id);
        _pendingLaunch     = nullptr;
        if (!e) return;

        stopApp();

        // Launching home unwinds the stack rather than growing it, so bouncing
        // between the launcher and a tool cannot run it out.
        if (std::strcmp(id, _homeId) == 0) {
            _depth = 0;
        }
        if (_depth < kStackDepth) {
            _stack[_depth++] = e->info->id;
        } else {
            _stack[kStackDepth - 1] = e->info->id;
        }
        startApp(*e);
    }
}

// -----------------------------------------------------------------------------
void AppManager::dispatchEvents() {
    auto& in = services().input();
    while (true) {
        const InputEvent ev = in.next();
        if (!ev) break;
        // Told here rather than sniffed from the queue: this is the one place
        // that sees every gesture, consumed or not.
        services().mood().noteInput(ev);

        // The press that lights a blanked panel is spent doing that. The device
        // may be recording a ride or serving a page, and pressing A to check on
        // it must not stop it. A dimmed panel is still readable, so those
        // presses go through as normal.
        if (_backlight == Backlight::Blank) {
            wakeBacklight();
            continue;
        }
        if (!_app) continue;

        if (_app->onEvent(ev)) continue;

        // Unconsumed navigation is handled centrally, so no app can trap the
        // user with a missing or broken exit.
        if (ev.type == EvType::Back) back();
        else if (ev.type == EvType::Menu) launch(_homeId);
    }
}

// -----------------------------------------------------------------------------
//  The backlight, which nothing was ever turning down
// -----------------------------------------------------------------------------
namespace {
/// The panel is the largest single load on a 250 mAh cell. Forty-five seconds
/// is longer than anybody stares at a screen without touching it or moving the
/// device; three minutes is long enough to have stopped watching.
constexpr uint32_t kDimAfterSec   = 45;
constexpr uint32_t kBlankAfterSec = 180;
/// Readable in a dark room at a fraction of the current of a readable one.
constexpr uint8_t  kDimBrightness = 12;
}  // namespace

/// Only the panel sleeps. The app is never told, never stopped and never loses
/// a capability: a ride goes on recording, a night goes on logging, the web
/// server goes on answering. What stops is 64 KB of SPI nobody is looking at.
void AppManager::updateBacklight() {
    const uint8_t now = M5.Display.getBrightness();
    // What to restore to, sampled while awake so it follows the app. Never
    // zero: IR CLONE blanks the panel deliberately while probing for display
    // coupling, and capturing that would restore into the dark.
    if (_backlight == Backlight::Awake && now > 0) _wakeBright = now;
    if (_wakeBright == 0) return;   // nothing sampled yet, so nothing to put back

    // The idle clock belongs to the mood model, which resets on a press and on
    // the device being picked up, so a tool watched while held never goes dark.
    const uint32_t idle = services().mood().idleSec();

    // The app's own number when it states one; see AppInfo::dimAfterSec. An app
    // dimming later than the blank would skip straight to dark, so the blank
    // moves out a minute past it.
    const uint32_t dimAfter   = _app ? _app->info().dimAfterSec : kDimAfterSec;
    const uint32_t blankAfter = dimAfter < kBlankAfterSec ? kBlankAfterSec : dimAfter + 60;

    if (idle < dimAfter) {
        wakeBacklight();
    } else if (idle >= blankAfter) {
        if (_backlight != Backlight::Blank) {
            _backlight = Backlight::Blank;
            M5.Display.setBrightness(0);
        }
    } else if (_backlight == Backlight::Awake) {
        _backlight = Backlight::Dimmed;
        M5.Display.setBrightness(kDimBrightness);
    }
}

void AppManager::wakeBacklight() {
    if (_backlight == Backlight::Awake) return;
    _backlight = Backlight::Awake;
    M5.Display.setBrightness(_wakeBright);
    // Draw at once rather than at the next drawHz slot: a screen lighting up on
    // a three-minute-old frame reads as a hang.
    _lastDrawMs = 0;
}

void AppManager::toast(const char* msg, uint16_t colour, uint32_t ms) {
    std::strncpy(_toast, msg ? msg : "", sizeof(_toast) - 1);
    _toast[sizeof(_toast) - 1] = '\0';
    _toastColour = colour ? colour : ui::theme::kAccent;
    _toastUntil  = millis() + ms;
    // Zero is the sentinel for "no toast", so the one deadline in four billion
    // that lands on it would silence the message.
    if (_toastUntil == 0) _toastUntil = 1;
}

void AppManager::requestCaps(CapMask extra) {
    if (_app) services().applyCaps(_app->info().needs | extra);
}

void AppManager::setRotation(uint8_t rotation) { applyRotation(rotation & 3); }

void AppManager::setChrome(bool on) { _chrome = on; }

void AppManager::suspendDrawing(uint32_t ms) {
    _drawSuspendUntil = millis() + ms;
    if (_drawSuspendUntil == 0) _drawSuspendUntil = 1;   // the same sentinel
}

bool AppManager::drawingSuspended() const {
    // Unsigned difference, not an absolute comparison: both deadlines are
    // millis() + ms, which wraps every 49.7 days, and an absolute test then
    // reads backwards for the length of the timeout.
    return _drawSuspendUntil != 0 &&
           static_cast<int32_t>(millis() - _drawSuspendUntil) < 0;
}

void AppManager::render(uint32_t nowMs) {
    if (!_app || _canvas.w() == 0) return;

    const AppInfo& info   = _app->info();
    const uint16_t accent = info.accent ? info.accent : ui::theme::kAccent;
    const bool     chrome = !info.fullscreen && _chrome;

    _canvas.fillSprite(ui::theme::kBg);

    if (chrome) {
        ui::StatusInfo st;
        st.title       = info.title;
        st.badge       = _app->badge();
        st.badgeColour = _app->badgeColour() ? _app->badgeColour() : accent;
        st.batteryPct  = services().power().percent();
        st.charging    = services().power().charging();
        st.libraryMode = systemSettings().libraryMode();
        st.accent      = accent;
        ui::drawStatusBar(_canvas, st);
    }

    _app->onDraw(_canvas);

    if (chrome) ui::drawFooter(_canvas, _app->hintA(), _app->hintB());

    // A toast is text, and an app with the chrome off has said it wants none.
    if (chrome && _toastUntil != 0 &&
        static_cast<int32_t>(nowMs - _toastUntil) < 0) {
        ui::drawToast(_canvas, _toast, _toastColour);
    }

    _canvas.pushSprite(0, 0);
}

// -----------------------------------------------------------------------------
void AppManager::loop() {
    M5.update();

    const uint32_t nowMs = millis();
    const uint32_t nowUs = micros();

    services().power().refresh(nowMs);
    services().input().poll(nowMs);
    // Ahead of the mood, which appraises the air this estimates.
    services().thermal().tick(nowMs);
    // The device has a life outside whichever app is open, so the model that
    // tracks it runs even when its own screen is not showing.
    services().mood().tick(nowMs);

    dispatchEvents();
    updateBacklight();

    if (_app) {
        const AppInfo& info = _app->info();

        const uint32_t dtUs = nowUs - _lastTickUs;
        _lastTickUs         = nowUs;
        _tickAccUs += dtUs;

        const uint32_t tickPeriodUs =
            (info.tickHz > 0) ? (1000000u / info.tickHz) : 0;
        if (tickPeriodUs == 0 || _tickAccUs >= tickPeriodUs) {
            const uint32_t elapsed = _tickAccUs;
            _tickAccUs             = 0;
            _app->onTick(elapsed);
        }

        const uint32_t drawPeriodMs = (info.drawHz > 0) ? (1000u / info.drawHz) : 33;
        if (_backlight != Backlight::Blank && !drawingSuspended() &&
            nowMs - _lastDrawMs >= drawPeriodMs) {
            _lastDrawMs = nowMs;
            render(nowMs);
        }
    }

    processPending();

    // Hand the idle time back so Wi-Fi, USB-CDC and the watchdog get serviced.
    delay(1);
}

}  // namespace sd
