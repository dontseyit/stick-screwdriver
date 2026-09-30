#include "web/Console.h"

#include "core/Log.h"

#include <DNSServer.h>
#include <WiFi.h>

#include <cstdio>
#include <cstring>

namespace sd {
namespace {

/// One of each, and only while an app holds them. Static rather than owned by
/// Console because WebServer's handlers are plain callables with no user
/// pointer, so they have to reach the server somehow, and one radio means one
/// server means one console.
WebServer* g_server = nullptr;
DNSServer* g_dns    = nullptr;

constexpr uint16_t kDnsPort = 53;

uint32_t g_served = 0;

/// Everything before the title. Split here rather than templated because the
/// title is the one part of the head that differs per page, and a page that had
/// to hand its own document over would be free to draw a different one, which
/// is how the stylesheet forked the last time.
const char kHeadTop[] PROGMEM =
    "<!doctype html><html lang=en><head><meta charset=utf-8>"
    "<meta name=viewport content=\"width=device-width,initial-scale=1\">"
    "<title>";

/// The frame's design, in one place, for every page on this device.
///
/// It was two stylesheets. NOTES had thirty rules and RIDES seventeen, eleven of
/// them the same rule written twice, and the copies had drifted: `td` had
/// different padding in each, and RIDES had redefined the `button` ELEMENT as a
/// small red delete link, so one markup meant two things on one console.
///
/// So this holds the vocabulary every page shares - the document, the headings,
/// tables, buttons, cards, links - and nothing that belongs to one page. A
/// chart's strokes, a form's box and a picture's width travel with the page that
/// wants them, through PageInfo::css. Nothing here names an app.
const char kStyle[] PROGMEM =
    "</title><style>"
    ":root{color-scheme:dark}"
    "*{box-sizing:border-box}"
    "body{margin:0;padding:16px;background:#0a0c10;color:#e6edf3;"
    "font:16px/1.5 -apple-system,BlinkMacSystemFont,'Segoe UI',Roboto,sans-serif}"
    "h1{margin:0 0 2px;font-size:15px;letter-spacing:.18em;color:#ffb300}"
    ".sub{margin:0 0 16px;font-size:12px;color:#7d8590}"
    "h2{margin:18px 0 4px;font-size:12px;letter-spacing:.14em;color:#39c5cf}"
    "h3{margin:14px 0 2px;font-size:11px;letter-spacing:.14em;color:#7d8590}"
    "a{color:#39c5cf}"
    "table{width:100%;border-collapse:collapse;font-size:13px}"
    "td{padding:3px 0;border-bottom:1px solid #161a22}"
    "td.k{color:#e6edf3}"
    "td.v{text-align:right;color:#7d8590;font-variant-numeric:tabular-nums}"
    "td.v .r{display:block;font-size:11px;color:#4a525e}"
    "button{font:inherit;border:0;border-radius:8px;padding:11px 16px;cursor:pointer}"
    ".save{width:100%;margin-top:8px;background:#ffb300;color:#0a0c10;font-weight:600}"
    ".del{background:transparent;color:#f85149;padding:4px 0;font-size:13px}"
    // One card, for anything a page lists one of per record. It was two
    // identical rules, `.n` for a note and `.ride` for a ride, in the days when
    // the frame knew what it was framing.
    ".card{background:#161a22;border:1px solid #2a313d;border-radius:8px;"
    "padding:12px 14px;margin-top:12px}"
    // Somewhere to go, as a target a thumb can hit. The chevron is drawn rather
    // than written, so a page listing these writes only the label.
    ".nav{display:block;background:#161a22;border:1px solid #2a313d;"
    "border-radius:8px;padding:13px 14px;margin-top:8px;color:#e6edf3;"
    "text-decoration:none}"
    // Doubled on purpose: the stylesheet receives one backslash, and CSS reads
    // \\203a as U+203A. Written singly it is an octal escape the compiler eats.
    ".nav:after{content:'\\203a';float:right;color:#4a525e}"
    ".foot{margin-top:22px;font-size:12px;color:#4a525e}"
    ".foot b{color:#7d8590;font-weight:600}";

/// After the page's own rules, whatever they were.
const char kHeadEnd[] PROGMEM = "</style></head><body>";

HTTPMethod httpMethodOf(Method m) {
    return (m == Method::Post) ? HTTP_POST : HTTP_GET;
}

/// Answer one request with the page that claimed it. The Page is a stack local
/// of the request, so the document is closed by its destructor however the page
/// chose to leave.
void serve(const PageInfo& info) {
    if (!g_server) return;
    ++g_served;
    Page p(*g_server, info);
    info.fn(p);
}

/// The captive-portal handshake. A phone checks connectivity by fetching a
/// known URL and comparing what comes back; anything other than the expected
/// answer means "there is a sign-in page here", and a redirect both says so and
/// points at it. Absolute, because the phone has to be told the host.
void elsewhere() {
    if (!g_server) return;
    char url[48];
    std::snprintf(url, sizeof(url), "http://%s/",
                  WiFi.softAPIP().toString().c_str());
    g_server->sendHeader("Location", url);
    g_server->send(302, "text/plain", "");
}

}  // namespace

// -----------------------------------------------------------------------------
void Page::open(const char* title) {
    if (_opened) return;
    _opened = true;

    // Chunked, with no length declared: a page is as long as what it is
    // listing, and measuring it first would mean building it twice.
    _srv.setContentLength(CONTENT_LENGTH_UNKNOWN);
    _srv.send(200, "text/html; charset=utf-8", "");
    _srv.sendContent_P(kHeadTop);
    _out.escaped(title ? title : "");
    _out.flush();
    _srv.sendContent_P(kStyle);
    // The page's own rules, inside the same <style> the frame opened: one
    // stylesheet to parse, and nothing for a second <style> to override.
    if (_info.css) { _out.put(_info.css); _out.flush(); }
    _srv.sendContent_P(kHeadEnd);
}

Page::~Page() {
    if (!_opened) return;

    // Written out as it stands, tags and all, so a page whose closing prose is
    // two paragraphs simply writes two. put() and not putf(): these run to
    // several hundred bytes and putf's scratch line is 512, which would
    // truncate the long ones in silence.
    if (_info.note) _out.put(_info.note);

    // Everywhere else worth going, derived from the registry rather than
    // written down. A page removed from the firmware leaves every other page's
    // footer by itself. Skipped for a page that asked for the same list in its
    // body: the front page is the navigation, and does not need it twice.
    if (!_linked) {
        const PageInfo* nav[PageRegistry::kMaxPages];
        const int       n = PageRegistry::instance().navFor(
            _info.path, nav, PageRegistry::kMaxPages);
        if (n > 0) {
            _out.put("<p class=foot>");
            for (int i = 0; i < n; ++i) {
                if (i) _out.put(" &middot; ");
                _out.putf("<a href=%s>%s</a>", nav[i]->path, nav[i]->nav);
            }
            _out.put("</p>");
        }
    }

    _out.put("</body></html>");
    _out.flush();
    // A zero-length chunk ends a chunked body; without it the phone waits for
    // more until it gives up, and the page renders late or not at all.
    _srv.sendContent("", 0);
}

void Page::links() {
    if (_linked) return;
    _linked = true;

    const PageInfo* nav[PageRegistry::kMaxPages];
    const int       n =
        PageRegistry::instance().navFor(_info.path, nav, PageRegistry::kMaxPages);
    for (int i = 0; i < n; ++i) {
        _out.putf("<a class=nav href=%s>%s</a>", nav[i]->path, nav[i]->nav);
    }
    // Nothing when nothing registered. A console with no pages but this one is
    // a firmware with no apps in it, which still has to answer.
}

uint32_t Page::id() const {
    if (!_srv.hasArg("id")) return 0;
    const long v = _srv.arg("id").toInt();
    return (v > 0) ? static_cast<uint32_t>(v) : 0u;
}

void Page::seeOther(const char* path) {
    _srv.sendHeader("Location", path);
    _srv.send(303, "text/plain", "");
}

// -----------------------------------------------------------------------------
Console& console() {
    static Console c;
    return c;
}

uint32_t Console::served() const { return g_served; }

bool Console::begin() {
    if (_up) return true;
    g_served = 0;

    g_server = new WebServer(kPort);
    if (!g_server) return false;

    PageRegistry& reg = PageRegistry::instance();
    for (size_t i = 0; i < reg.count(); ++i) {
        const PageInfo* info = &reg.at(i);
        g_server->on(info->path, httpMethodOf(info->method),
                     [info] { serve(*info); });
    }

    // Everything else, which on a captive portal is every connectivity check
    // the phone makes, and exactly what we want to be asked.
    g_server->onNotFound(elsewhere);
    g_server->begin();

    g_dns = new DNSServer();
    if (g_dns) {
        // One answer for every question asked. The TTL is short so a phone that
        // caches it does not go on believing the world is at 192.168.4.1 after
        // it has left.
        g_dns->setTTL(30);
        g_dns->setErrorReplyCode(DNSReplyCode::NoError);
        if (!g_dns->start(kDnsPort, "*", WiFi.softAPIP())) {
            SD_LOGW("web", "dns would not start - portal will not pop");
            delete g_dns;
            g_dns = nullptr;
        }
    }

    _up = true;
    SD_LOGI("web", "console up on port %u, %u pages",
            static_cast<unsigned>(kPort), static_cast<unsigned>(reg.count()));
    return true;
}

void Console::end() {
    if (g_dns) { g_dns->stop(); delete g_dns; g_dns = nullptr; }
    if (g_server) { g_server->stop(); delete g_server; g_server = nullptr; }
    _up = false;
}

void Console::poll() {
    if (!_up) return;
    // DNS first: a phone that cannot resolve does not get as far as the server.
    if (g_dns) g_dns->processNextRequest();
    if (g_server) g_server->handleClient();
}

}  // namespace sd
