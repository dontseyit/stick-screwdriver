// -----------------------------------------------------------------------------
//  HELP - one page per tool: what it is, how it is driven, and the thing that
//  will catch you out.
//
//  The three strings live in each app's AppInfo, not in a table here, so a new
//  tool arrives by existing and help cannot drift. A tool with no page is absent.
//
//  The panel is 240x135: 86 rows of prose once the title and its rule are paid
//  for, nine lines at Font0, with block gaps eating half of one. The budget is
//
//      what   3 lines, full width      about 110 characters
//      USE    2 lines, indented        about  62
//      CARE   3 lines, indented        about  94
//
//  Font0 advances a fixed six pixels per glyph, so page height is arithmetic;
//  tools/help_fit.py does the sum for every page. Width is a font metric, not a
//  constant, so nothing checks it at compile time: the debug build logs the
//  page that ran over and the panel shows an ellipsis.
// -----------------------------------------------------------------------------
#include "core/App.h"
#include "core/AppRegistry.h"
#include "core/Log.h"
#include "ui/Widgets.h"

#include <cstdio>

namespace sd {
namespace {

constexpr AppInfo kInfo{
    .id       = "help",
    .title    = "HELP",
    .subtitle = "what each tool is for",
    .group    = "SYS",
    .needs    = caps(Cap::Display),
    .rotation = 1,
    .tickHz   = 10,
    .drawHz   = 10,
    // A list and a cursor; 160 MHz is plenty.
    .cpuMhz   = 160,
    .accent   = ui::theme::kCyan,
    .what     = "A page per tool: what it is, how it is driven, and what will catch you out.",
    .how      = "B is the next page, A the one before.",
    .care     = "Each page is written by the tool itself, so a new tool arrives here on its own.",
};

/// Grammar table on the opening page. B-hold and A+B are fixed by the framework.
struct Gesture {
    const char* keys;
    const char* means;
};
constexpr Gesture kGrammar[] = {
    {"A",       "primary action"},
    {"A hold",  "secondary, repeats"},
    {"B",       "cycle mode or page"},
    {"B hold",  "BACK - always, everywhere"},
    {"A + B",   "menu"},
};
constexpr int kGrammarRows = static_cast<int>(sizeof(kGrammar) / sizeof(kGrammar[0]));

constexpr int kLineH = 9;

}  // namespace

// -----------------------------------------------------------------------------
class HelpApp : public App {
public:
    void onStart() override {
        _count = 0;
        const AppRegistry& reg = AppRegistry::instance();
        for (size_t i = 0; i < reg.count() && _count < kMax; ++i) {
            if (reg.at(i).info->what) _index[_count++] = static_cast<uint8_t>(i);
        }
        _page = 0;
    }

    bool onEvent(const InputEvent& ev) override {
        const int pages = _count + 1;
        if (ev.click(Btn::B)) { _page = (_page + 1) % pages; return true; }
        if (ev.click(Btn::A)) { _page = (_page + pages - 1) % pages; return true; }
        return false;   // B-hold leaves, as it does everywhere
    }

    void onDraw(ui::Canvas& c) override {
        if (_page == 0) drawGrammar(c);
        else            drawEntry(c, AppRegistry::instance().at(_index[_page - 1]));
    }

    const char* hintA() const override { return "A back"; }
    const char* hintB() const override { return "B next   hold B  leave"; }

    const char* badge() const override {
        // Sized for two full-width ints: the bound the compiler can see.
        static char buf[32];
        std::snprintf(buf, sizeof(buf), "%d/%d", _page + 1, _count + 1);
        return buf;
    }

private:
    void drawGrammar(ui::Canvas& c) {
        const int top = ui::theme::kBarH;
        c.textAt(6, top + 2, "Two buttons", ui::theme::kText, &fonts::Font2,
                 top_left);
        c.rule(top + 20, ui::theme::kLine, 4);

        int y = top + 26;
        for (int i = 0; i < kGrammarRows; ++i) {
            c.textAt(8, y, kGrammar[i].keys, ui::theme::kCyan, &fonts::Font0,
                     top_left);
            c.textAt(64, y, kGrammar[i].means, ui::theme::kText, &fonts::Font0,
                     top_left);
            y += kLineH;
        }

        y += 4;
        c.rule(y, ui::theme::kLine, 4);
        y += 5;
        ui::drawWrapped(c, 6, y, c.w() - 12, c.h() - ui::theme::kFootH, kLineH,
                        "The last two are the framework's rather than any "
                        "app's, so nothing you open can trap you.",
                        ui::theme::kDim, &fonts::Font0);
    }

    void drawEntry(ui::Canvas& c, const AppEntry& e) {
        const AppInfo& a    = *e.info;
        const int      top  = ui::theme::kBarH;
        const int      maxY = c.h() - ui::theme::kFootH;
        const uint16_t accent = a.accent ? a.accent : ui::theme::kAccent;

        // No subtitle: it repeats the first sentence of `what` and costs a row.
        c.textAt(6, top + 1, a.title, accent, &fonts::Font2, top_left);
        c.textAt(c.w() - 6, top + 5, a.group, ui::theme::kFaint, &fonts::Font0,
                 top_right);
        c.rule(top + 19, ui::theme::kLine, 4);

        int y = top + 23;
        y = ui::drawWrapped(c, 6, y, c.w() - 12, maxY, kLineH, a.what,
                            ui::theme::kText, &fonts::Font0);
        y += 3;
        y = block(c, y, maxY, "USE", a.how, ui::theme::kCyan);
        y += 2;
        y = block(c, y, maxY, "CARE", a.care, ui::theme::kWarn);

        if (y > maxY) {
            // Clipped mid-sentence; say so rather than look like the end.
            c.textAt(c.w() - 6, maxY - kLineH, "...", ui::theme::kErr,
                     &fonts::Font0, top_right);
            SD_LOGW("help", "'%s' page overflows by %d px", a.id, y - maxY);
        }
    }

    /// A labelled paragraph. The label has its own column so blocks line up.
    static int block(ui::Canvas& c, int y, int maxY, const char* label,
                     const char* text, uint16_t colour) {
        if (!text || !*text) return y;
        if (y + kLineH <= maxY) {
            c.textAt(6, y, label, colour, &fonts::Font0, top_left);
        }
        return ui::drawWrapped(c, 40, y, c.w() - 46, maxY, kLineH, text,
                               ui::theme::kText, &fonts::Font0);
    }

    static constexpr int kMax = 48;
    uint8_t _index[kMax]{};
    int     _count = 0;
    int     _page  = 0;
};

SD_REGISTER_APP(HelpApp, kInfo)

}  // namespace sd
