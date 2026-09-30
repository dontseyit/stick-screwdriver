// -----------------------------------------------------------------------------
//  IndexPage - what a phone is shown first: this device's own numbers, and a
//  button per page that registered itself.
//
//  It belongs to the base rather than to an app, and that is the whole point.
//  The front page used to be NOTES' form, for the accidental reason that NOTES
//  was the first app to want a phone attached, so deleting apps/notes/ took the
//  console's root page with it - and the captive portal redirects EVERY request
//  to "/", so the device would have answered a phone with a redirect to a
//  redirect. A firmware with no apps compiled in still serves this.
//
//  It knows no app either. The buttons are the registry's nav labels, so a page
//  arrives here by existing and leaves by being deleted, and this file is not
//  edited for either.
//
//  The table is the raw reading and nothing else: no mood, no interpretation, no
//  history. That is /device's job and it asks the services for all of it. What
//  is here is what somebody holding the device wants confirmed in one glance -
//  is it charging, is it hot, is it running out of memory - and none of it was
//  on the web at all before, the panel's SYSTEM app being the only place to
//  read it.
// -----------------------------------------------------------------------------
#include "mathx/Thermal.h"
#include "mathx/Words.h"
#include "services/PowerService.h"
#include "services/RadioService.h"
#include "services/ServiceHub.h"
#include "services/ThermalService.h"
#include "web/Console.h"

#include <Arduino.h>
#include <esp_heap_caps.h>

namespace sd {
namespace {

/// The dies as read, and only the ones that answered. A sensor that said
/// nothing is left out rather than shown as zero: zero is a temperature.
///
/// The PMIC is absent on purpose. Its TEMP channel reads millivolts with no
/// published transfer function, so the one place it is shown says so in full;
/// see services/ThermalService.h. A row here would be a number pretending.
void dies(WebOut& o) {
    const ThermalReading& r = services().thermal().reading();
    for (int i = 0; i < kTempSourceCount; ++i) {
        const TempSource s = static_cast<TempSource>(i);
        if (s == TempSource::Pmic || !r.ok[i]) continue;
        o.row(tempSourceName(s), "%.1f &deg;C", static_cast<double>(r.c[i]));
    }
}

void vitals(WebOut& o) {
    auto& pwr = services().power();
    char  buf[32];

    o.put("<h2>DEVICE</h2><table>");

    sayDurationLong(millis() / 1000u, buf, sizeof(buf));
    o.row("up", "%s", buf);

    // Before the first poll the percentage is -1, which is not a charge level.
    if (pwr.percent() >= 0) {
        o.row("battery", "%d %%<span class=r>%.2f V%s</span>", pwr.percent(),
              static_cast<double>(pwr.volts()),
              pwr.charging() ? " &middot; charging" : "");
    } else {
        o.row("battery", "%.2f V", static_cast<double>(pwr.volts()));
    }

    o.row("heap free", "%u KB",
          static_cast<unsigned>(ESP.getFreeHeap() >> 10));
    o.row("psram free", "%u / %u KB",
          static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) >> 10),
          static_cast<unsigned>(heap_caps_get_total_size(MALLOC_CAP_SPIRAM) >> 10));
    dies(o);
    o.row("cpu", "%u MHz", static_cast<unsigned>(getCpuFrequencyMhz()));

    o.put("</table>");
}

// -----------------------------------------------------------------------------
void indexPage(Page& p) {
    p.open();
    WebOut& o = p.out();

    auto&              radio  = services().radio();
    const int          phones = radio.apClients();
    const unsigned long served = static_cast<unsigned long>(console().served());

    o.put("<h1>SCREWDRIVER</h1>");
    o.putf("<p class=sub>%s &middot; %d phone%s &middot; %lu page%s served</p>",
           radio.apAddress(), phones, phones == 1 ? "" : "s", served,
           served == 1 ? "" : "s");

    // The navigation, in the body and as buttons: on the front page it IS the
    // page. Asking for it here also keeps it out of this page's own footer.
    p.links();

    vitals(o);
}

const char kNote[] PROGMEM =
    "<p class=foot>Read as the device is running. Nothing above is stored for "
    "this page and nothing is computed until you ask for it.</p>"
    "<p class=foot>The list is whatever this firmware has: every page comes "
    "from the feature that owns it, so one that was left out is simply not "
    "here.</p>";

constexpr PageInfo kIndexPage{
    .path   = "/",
    .method = Method::Get,
    .title  = "SCREWDRIVER",
    .nav    = "the front page",
    .note   = kNote,
    .fn     = indexPage,
};

SD_REGISTER_PAGE(IndexPage, kIndexPage)

}  // namespace
}  // namespace sd
