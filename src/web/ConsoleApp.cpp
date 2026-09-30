// -----------------------------------------------------------------------------
//  CONSOLE - this device, on a phone.
//
//  It used to be half of NOTES, for the accidental reason that NOTES was the
//  first thing that wanted a phone attached. Everything else the console serves
//  arrived afterwards and had to be wired in through that app: the rides, the
//  device report, the store inventory, and a gallery since removed. None of
//  them are notes.
//
//  So raising the network is its own job. This app owns the access point, the
//  key and the server; the pages come from wherever they belong, through
//  web/PageRegistry.h. NOTES went back to being a notepad, at /notes, and the
//  front page is the base's own: web/IndexPage.cpp, which knows no app and works
//  in a firmware with none.
//
//  Closed by default. The access point is WPA2 with a key generated once per
//  device and shown on the screen. An open one would be less to type, and would
//  also mean whatever the pages are serving - the shopping list, the door code,
//  the note taken at the doctor's - is readable by anyone within thirty metres
//  while the app is open.
//
//  The key comes from esp_random() over an alphabet with no O/0 or I/l/1,
//  because it is read off a 240x135 panel and typed into a phone. It is stored
//  under this app's own id, so a device that ran the older firmware generates a
//  fresh one the first time this app opens, costing a rescan of the QR.
//
//  The chrome is off because the QR payload needs version 3, which is 29
//  modules; a module has to be at least three pixels to scan comfortably; and
//  four modules of quiet zone are what the standard asks for. 29*3 + 8*3 = 111
//  against a 135-tall panel, so the code is the largest that fits only with the
//  status bar off, and this screen draws its own way out along the bottom.
// -----------------------------------------------------------------------------
#include "core/App.h"
#include "core/AppManager.h"
#include "mathx/WifiQr.h"
#include "services/ServiceHub.h"
#include "services/Settings.h"
#include "ui/Widgets.h"
#include "web/Console.h"

#include <esp_random.h>

#include <cstdio>
#include <cstring>

namespace sd {
namespace {

constexpr AppInfo kInfo{
    .id       = "console",
    .title    = "CONSOLE",
    .subtitle = "this device, on a phone",
    .group    = "SYS",
    .needs    = Cap::Display | Cap::WiFi,
    .rotation = 1,
    // Above the loop's own rate on purpose: the web server is synchronous, so a
    // request waits exactly as long as the gap between polls.
    .tickHz   = 1000,
    .drawHz   = 8,
    .accent   = ui::theme::kAccent,
    .what     = "Raises its own WiFi network and serves this device to a phone: the numbers it reads, and a page per feature.",
    .how      = "Scan the code to join. The front page opens by itself; everything else is a button on it. Hold A for a new key.",
    .care     = "The network exists only while this app is open.",
};

/// No O/0 and no I/l/1: this is read off a small panel and typed into a phone.
constexpr char kKeyAlphabet[] = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
constexpr int  kKeyLen        = 8;   ///< WPA2's minimum, and nobody wants more

/// Fixed, and not suffixed with the MAC. Asked for by name, and with the key in
/// the QR there is nothing left to type, so the one thing a suffix bought
/// (telling two of these apart) costs more than it is worth. Two in one room
/// would be two networks of one name with different keys, and a phone would
/// pick by signal strength and fail to join one.
constexpr const char* kSsid = "Screwdriver";

constexpr uint8_t kApChannel = 6;
constexpr int     kQrBox     = 111;

constexpr const char* kNs     = "console";
constexpr const char* kKeyKey = "apkey";

class ConsoleApp : public App {
public:
    void onStart() override { loadKey(); }

    void onStop() override {
        console().end();
        // Borrowed for the QR; the canvas is the framework's and every app
        // after this one draws on it.
        apps().setChrome(true);
        // The radio goes down with Cap::WiFi, which is the point of asking for
        // the capability rather than raising the AP behind the framework's back.
    }

    void onTick(uint32_t) override {
        // Nothing blocking runs until a frame has been shown. Bringing up an
        // access point takes a moment, and doing it before anything is drawn
        // makes the app look like it has hung.
        if (!_drawn) return;
        if (!_booted) { boot(); return; }
        console().poll();
    }

    bool onEvent(const InputEvent& ev) override {
        // A held A is the only thing this screen does. Back is the framework's
        // and is deliberately not consumed here.
        if (ev.longPress(Btn::A)) { newKey(); return true; }
        return false;
    }

    void onDraw(ui::Canvas& c) override {
        if (!_drawn) {
            _drawn = true;
            // Not in onStart: the QR needs the whole panel and setChrome only
            // takes effect on the next frame, so the first frame would be drawn
            // with a status bar over the top of it.
            apps().setChrome(false);
        }

        if (_fault[0]) {
            c.textAt(c.w() / 2, c.h() / 2, _fault, ui::theme::kErr,
                     &fonts::Font2, middle_center);
            return;
        }

        auto& radio = services().radio();

        // Scanned, not typed. "Join Screwdriver, then find the key, then type
        // it" is three instructions and each one is somewhere to stop; a camera
        // pointed at this is none.
        if (_qr[0]) c.qrcode(_qr, 6, 12, kQrBox, 1, /*margin*/ true);

        const int col = 6 + kQrBox + 9;
        c.textAt(col, 24, "SCAN TO JOIN", ui::theme::kDim, &fonts::Font0,
                 middle_left);
        c.textAt(col, 44, kSsid, ui::theme::kAccent, &fonts::Font2, middle_left);

        // Shown as well as encoded, because a camera that will not focus is a
        // thing that happens and the network is still joinable by hand.
        c.textAt(col, 62, "KEY", ui::theme::kDim, &fonts::Font0, middle_left);
        c.textAt(col, 78, radio.apUp() ? _key : "--", ui::theme::kText,
                 &fonts::Font2, middle_left);

        const int n = radio.apClients();
        c.textf(col, 100, n ? ui::theme::kOk : ui::theme::kFaint, &fonts::Font0,
                middle_left, n ? "%d phone%s on" : "no phone yet", n,
                n == 1 ? "" : "s");

        // The number that separates "nobody joined" from "the page will not
        // load", which look the same from in front of the device.
        const unsigned long served =
            static_cast<unsigned long>(console().served());
        c.textf(col, 112, ui::theme::kFaint, &fonts::Font0, middle_left,
                served == 1 ? "%lu page served" : "%lu pages served", served);

        // Its own footer: the chrome is off so the QR can have three pixels a
        // module, and a screen with no way off it is worse than a small QR.
        c.textAt(6, 130, "hold A new key   hold B back", ui::theme::kFaint,
                 &fonts::Font0, middle_left);
    }

    const char* badge() const override { return nullptr; }

private:
    void boot() {
        _booted = true;
        if (!services().radio().beginAp(kSsid, _key, kApChannel)) {
            std::snprintf(_fault, sizeof(_fault), "the radio refused AP mode");
            return;
        }
        if (!console().begin()) {
            std::snprintf(_fault, sizeof(_fault), "no memory for the server");
            return;
        }
        buildQr();
    }

    void loadKey() {
        Settings st(kNs, /*readOnly*/ true);
        char     buf[kKeyLen + 1]{};
        if (st.ok() && st.getBlob(kKeyKey, buf, kKeyLen) && buf[0]) {
            std::memcpy(_key, buf, kKeyLen);
            _key[kKeyLen] = '\0';
            buildQr();
            return;
        }
        newKey();
    }

    void newKey() {
        constexpr int kAlphabet = static_cast<int>(sizeof(kKeyAlphabet)) - 1;
        for (int i = 0; i < kKeyLen; ++i) {
            // The alphabet is 32 characters, so this is a clean five bits and
            // not a modulo with a bias in it.
            _key[i] = kKeyAlphabet[esp_random() % static_cast<uint32_t>(kAlphabet)];
        }
        _key[kKeyLen] = '\0';

        Settings st(kNs, /*readOnly*/ false);
        if (st.ok()) st.putBlob(kKeyKey, _key, kKeyLen);

        if (_booted && !_fault[0]) {
            // The network has to be rebuilt around it; a key changed underneath
            // a running AP is a key only the screen believes in.
            services().radio().beginAp(kSsid, _key, kApChannel);
        }
        buildQr();
    }

    /// The string a phone camera turns into "join this network?".
    void buildQr() { wifiQrPayload(kSsid, _key, _qr, sizeof(_qr)); }

    char _key[kKeyLen + 1]{};
    char _qr[96]{};
    char _fault[48]{};
    bool _booted = false;
    bool _drawn  = false;
};

SD_REGISTER_APP(ConsoleApp, kInfo)

}  // namespace
}  // namespace sd
