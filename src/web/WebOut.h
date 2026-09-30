#pragma once
// -----------------------------------------------------------------------------
//  WebOut - one buffer between a page and the socket, and why it is not
//  optional.
//
//  A page here is two hundred small pieces, a row of a table, a number, a link,
//  and sendContent() on each is two hundred chunked writes down a TCP socket,
//  every one blocking the loop task that also runs the display and the input.
//  That is not a slow page, it is a device that stops answering its buttons
//  while somebody looks at a table.
//
//  Accumulated into a kilobyte and flushed when full instead: the same page in
//  six or seven writes. The buffer is a stack local of the request, so it costs
//  nothing when nobody is looking.
//
//  It is shared rather than copied per page for a reason worth writing down:
//  the second page to want it started as a copy of the first, and the copy came
//  with a smaller scratch buffer for putf(). vsnprintf truncates in silence,
//  and the rows of that page came out cut off mid-tag.
//
//  It stops the moment the reader goes away, and that is not an optimisation.
//  NetworkClient::write retries ten times with a one-second select on each, so
//  a single write to a phone that has stopped reading blocks for up to TEN
//  SECONDS. The server is synchronous and runs from loop(), so that is ten
//  seconds in which this device answers nothing: not the buttons, not the
//  screen, not DHCP, not ARP. A page of a few chunks survives that; a ride is a
//  hundred, and a phone closing the sheet mid-download, which is what a captive
//  portal does when handed a file it cannot open, takes the whole device off
//  the air for minutes while the AP is still beaconing.
//
//  So every chunk asks whether anyone is still there first. connected() is a
//  non-blocking peek at the socket, and once the answer is no this stops
//  writing and stops copying, turning the rest of a large page into some
//  microseconds of formatting nobody sees.
// -----------------------------------------------------------------------------
#include "mathx/NoteText.h"

#include <WebServer.h>

#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace sd {

class WebOut {
public:
    explicit WebOut(WebServer& s) : _s(s) {}
    ~WebOut() { flush(); }

    WebOut(const WebOut&)            = delete;
    WebOut& operator=(const WebOut&) = delete;

    /// True once the reader has gone. Worth asking in any loop long enough to
    /// matter: there is nothing left to build a page for.
    bool gone() {
        if (_gone) return true;
        // Cheap: a peek with MSG_DONTWAIT, not a read.
        if (_s.client().connected()) return false;
        _gone = true;
        return true;
    }

    void put(const char* text) {
        if (!text || gone()) return;
        const size_t n = std::strlen(text);
        if (n >= sizeof(_buf)) {   // longer than the buffer: send it on its own
            flush();
            _s.sendContent(text, n);
            return;
        }
        if (_used + n >= sizeof(_buf)) flush();
        std::memcpy(_buf + _used, text, n);
        _used += n;
    }

    /// The scratch line is generous on purpose. These are HTML templates, so
    /// the format string alone runs to a few hundred bytes before a single
    /// value is substituted, and the failure when it does not fit is a page
    /// silently missing the end of a tag.
    void putf(const char* fmt, ...) __attribute__((format(printf, 2, 3))) {
        char    line[512];
        va_list ap;
        va_start(ap, fmt);
        std::vsnprintf(line, sizeof(line), fmt, ap);
        va_end(ap);
        put(line);
    }

    /// Anything that came from outside the page building it. App titles come
    /// from their own AppInfo and reasons are built by the appraiser, so
    /// neither is hostile, but neither is a page's to vouch for.
    void escaped(const char* text) {
        char        chunk[192];
        const char* p = text ? text : "";
        while (*p) {
            const char* next = nullptr;
            htmlEscape(p, chunk, sizeof(chunk), &next);
            if (next == p) break;   // no progress is the end, whatever the input
            put(chunk);
            p = next;
        }
    }

    void row(const char* label, const char* fmt, ...)
        __attribute__((format(printf, 3, 4))) {
        char    val[192];
        va_list ap;
        va_start(ap, fmt);
        std::vsnprintf(val, sizeof(val), fmt, ap);
        va_end(ap);
        putf("<tr><td class=k>%s</td><td class=v>%s</td></tr>", label, val);
    }

    void flush() {
        if (!_used) return;
        // Dropped rather than written. What is in the buffer was for somebody
        // who is no longer listening, and the write is what costs the device
        // ten seconds.
        if (!gone()) _s.sendContent(_buf, _used);
        _used = 0;
    }

private:
    WebServer& _s;
    char       _buf[1024];
    size_t     _used = 0;
    bool       _gone = false;
};

}  // namespace sd
