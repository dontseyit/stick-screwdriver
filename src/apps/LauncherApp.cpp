// -----------------------------------------------------------------------------
//  Launcher - the home app.
//
//  It has no list of its own; it renders the AppRegistry minus itself and minus
//  anything with a parent. Adding a tool needs no edit to this file.
// -----------------------------------------------------------------------------
#include "core/App.h"
#include "core/AppManager.h"
#include "core/AppRegistry.h"
#include "ui/Widgets.h"

namespace sd {
namespace {

constexpr AppInfo kInfo{
    .id       = "launcher",
    .title    = "SCREWDRIVER",
    .subtitle = "tool index",
    .group    = "SYS",
    .needs    = caps(Cap::Display),
    .rotation = 0,   // portrait: a list wants the height
    .tickHz   = 20,
    .drawHz   = 20,
    // A list and a cursor; 160 MHz is plenty.
    .cpuMhz   = 160,
    .accent   = ui::theme::kAccent,
    .what     = "The launcher. Every tool by name, pinned ones first. Folders hold what named them.",
    .how      = "B moves the cursor, A opens.",
    .care     = "Hold B in any tool to come back. The framework enforces it.",
};

class LauncherApp : public App {
public:
    void onStart() override {
        buildIndex();
        _menu.setCount(_count);
        // Coming back from a tool should land on that tool, not at the top.
        for (int i = 0; i < _count; ++i) {
            if (_index[i] == _lastLaunched) { _menu.select(i); break; }
        }
    }

    bool onEvent(const InputEvent& ev) override {
        if (ev.click(Btn::B) || (ev.type == EvType::Repeat && ev.btn == Btn::B)) {
            _menu.next();
            return true;
        }
        if (ev.longPress(Btn::A)) { _menu.prev(); return true; }
        if (ev.click(Btn::A)) {
            if (_count > 0) {
                _lastLaunched = _index[_menu.selected()];
                apps().launch(entry(_menu.selected()).info->id);
            }
            return true;
        }
        // Back has nowhere to go here; swallow it to avoid a pointless restart.
        if (ev.type == EvType::Back || ev.type == EvType::Menu) return true;
        return false;
    }

    void onDraw(ui::Canvas& c) override {
        const int top = ui::theme::kBarH + 3;
        const int bot = c.h() - ui::theme::kFootH - 2;
        _menu.draw(c, top, bot, &provide, this, kInfo.accent);
    }

    const char* hintA() const override { return "A open"; }
    const char* hintB() const override { return "B next"; }

private:
    static constexpr int kMax = AppRegistry::kMaxApps;

    void buildIndex() {
        const auto& reg = AppRegistry::instance();
        _count          = 0;
        for (size_t i = 0; i < reg.count() && _count < kMax; ++i) {
            const AppInfo& a = *reg.at(i).info;
            if (&a == &kInfo) continue;   // never list ourselves
            if (a.parent) continue;       // listed by its parent instead
            _index[_count++] = static_cast<int>(i);
        }
    }

    const AppEntry& entry(int slot) const {
        return AppRegistry::instance().at(static_cast<size_t>(_index[slot]));
    }

    static void provide(int i, ui::MenuItem& out, void* ctx) {
        auto* self       = static_cast<LauncherApp*>(ctx);
        const AppInfo& a = *self->entry(i).info;
        out.title        = a.title;
        out.subtitle     = a.subtitle;
        out.accent       = a.accent;
    }

    ui::ListMenu _menu;
    int          _index[kMax]{};
    int          _count = 0;

    /// Survives app teardown so the cursor is where you left it.
    static int _lastLaunched;
};

int LauncherApp::_lastLaunched = -1;

}  // namespace

SD_REGISTER_APP(LauncherApp, kInfo)

}  // namespace sd
