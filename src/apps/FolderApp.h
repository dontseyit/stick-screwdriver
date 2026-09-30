#pragma once
// -----------------------------------------------------------------------------
//  FolderApp - a launcher entry that is a list of other apps.
//
//  A folder holds no list. A tool names the folder as its parent in its own
//  AppInfo, the launcher skips anything with a parent, and the folder lists
//  whatever named it. Each tool keeps its AppInfo, hardware, HELP page and
//  saved state; Back inside one returns to the folder via AppManager's stack.
//
//  One template, not one file per folder: a folder's .cpp is an AppInfo and one
//  line. That AppInfo must have EXTERNAL linkage: write `inline constexpr
//  AppInfo kSomethingFolder` at namespace scope, never the `constexpr AppInfo
//  kInfo` in an anonymous namespace the rest of this tree uses. The anonymous
//  form compiles and links, then shows every folder's title over a wrong list.
//
//  A template argument naming an object in an anonymous namespace mangles to
//  `_GLOBAL__N_1` with nothing identifying the translation unit, so four folders
//  emit four instantiations under one name. Instantiations are weak symbols: the
//  linker keeps the first and drops the rest, every folder runs that one's code
//  bound to that one's kInfo, buildIndex() filters on the wrong id, and
//  s_lastLaunched becomes one shared cursor. Titles stay right because
//  AppManager reads them from the registry. `constexpr` at namespace scope
//  implies `const` and internal linkage; `inline` makes the object external.
// -----------------------------------------------------------------------------
#include "core/App.h"
#include "core/AppManager.h"
#include "core/AppRegistry.h"
#include "core/Log.h"
#include "ui/Widgets.h"

#include <cstring>

namespace sd {

template <const AppInfo& kInfo>
class FolderApp : public App {
public:
    void onStart() override {
        buildIndex();
        _menu.setCount(_count);
        // Coming back from a tool should land on that tool, not at the top.
        for (int i = 0; i < _count; ++i) {
            if (_index[i] == s_lastLaunched) { _menu.select(i); break; }
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
                s_lastLaunched = _index[_menu.selected()];
                apps().launch(entry(_menu.selected()).info->id);
            }
            return true;
        }
        return false;   // Back leaves for the launcher, as it does everywhere
    }

    void onDraw(ui::Canvas& c) override {
        const int top = ui::theme::kBarH + 3;
        const int bot = c.h() - ui::theme::kFootH - 2;
        _menu.draw(c, top, bot, &provide, this, info().accent);
    }

    const char* hintA() const override { return "A open"; }
    const char* hintB() const override { return "B next"; }

private:
    static constexpr int kMax = AppRegistry::kMaxApps;

    void buildIndex() {
        // info(), not kInfo: if the linkage rule above is ever broken, the one
        // AppManager handed this instance is still right. The check then logs.
        const AppInfo& self = info();
        if (std::strcmp(self.id, kInfo.id) != 0) {
            SD_LOGE("folder", "'%s' is running '%s' code - see FolderApp.h",
                    self.id, kInfo.id);
        }

        const auto& reg = AppRegistry::instance();
        _count          = 0;
        for (size_t i = 0; i < reg.count() && _count < kMax; ++i) {
            const char* parent = reg.at(i).info->parent;
            if (parent && std::strcmp(parent, self.id) == 0) {
                _index[_count++] = static_cast<int>(i);
            }
        }
    }

    const AppEntry& entry(int slot) const {
        return AppRegistry::instance().at(static_cast<size_t>(_index[slot]));
    }

    static void provide(int i, ui::MenuItem& out, void* ctx) {
        auto* self       = static_cast<FolderApp*>(ctx);
        const AppInfo& a = *self->entry(i).info;
        out.title        = a.title;
        out.subtitle     = a.subtitle;
        out.accent       = a.accent;
    }

    ui::ListMenu _menu;
    int          _index[kMax]{};
    int          _count = 0;

    /// Survives teardown so the cursor stays put. One per folder instantiation.
    static int s_lastLaunched;
};

template <const AppInfo& kInfo>
int FolderApp<kInfo>::s_lastLaunched = -1;

}  // namespace sd
