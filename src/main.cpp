// -----------------------------------------------------------------------------
//  Screwdriver - a pocket multi-tool for the M5Stack StickS3.
//
//  main() does almost nothing: bring up the board, bring up the services, hand
//  control to the AppManager. Every feature is an app registered from its own
//  translation unit, so this file never changes when one is added.
// -----------------------------------------------------------------------------
#include <M5Unified.h>

#include "core/AppManager.h"
#include "core/AppRegistry.h"
#include "core/Log.h"
#include "services/ServiceHub.h"

namespace {
constexpr const char* kHomeApp = "launcher";
}

void setup() {
    auto cfg = M5.config();
    cfg.serial_baudrate = 115200;
    cfg.clear_display   = true;
    cfg.internal_imu    = true;
    // These only *configure* the codec: M5.begin() populates the I2S pins and
    // the ES8311 power-up callback but starts nothing. Leave them false and the
    // config is never committed, so a later Speaker.begin() has no pins and
    // fails silently. Power is still claimed on demand via Cap::Speaker /
    // Cap::Mic and Speaker.begin()/end().
    cfg.internal_spk    = true;
    cfg.internal_mic    = true;
    cfg.output_power    = false;  // EXT_5V is refcounted by PowerService
    M5.begin(cfg);

    M5.Display.setBrightness(110);

    SD_LOGI("boot", "Screwdriver up: %d apps registered",
            static_cast<int>(sd::AppRegistry::instance().count()));

    sd::services().begin();

    if (!sd::apps().begin(kHomeApp)) {
        M5.Display.setRotation(1);
        M5.Display.fillScreen(TFT_BLACK);
        M5.Display.setTextColor(TFT_RED);
        M5.Display.setFont(&fonts::Font2);
        M5.Display.setCursor(4, 20);
        M5.Display.print("boot failed");
        while (true) delay(1000);
    }
}

void loop() { sd::apps().loop(); }
