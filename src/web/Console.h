#pragma once
// -----------------------------------------------------------------------------
//  Console - the web console: one server, one design, and pages that arrive by
//  registering themselves.
//
//  It owns the socket, the captive portal, and the frame every page is drawn
//  in: the document, the stylesheet, the footer prose and the links to
//  everywhere else. It owns no content. A page is a function somewhere else in
//  the tree that writes a body, and web/PageRegistry.h is how it says so.
//
//  The split is the point. Before it, adding a page meant editing NOTES, the
//  app that happens to raise the network, in five places, and other features'
//  pages lived in its directory: RIDE's chart was in apps/notes/, and so was
//  the removed PLACE gallery, which is why taking it out meant hunting through
//  another app's file for five routes and two footers.
//
//  Chrome is asked for, not imposed. A page calls open() when it has decided to
//  answer with a document. One that answers some other way, a PNG, a CSV, a 404
//  for a ride that is not there, a redirect after a POST, never calls it, and
//  nothing is wrapped around a response that was not HTML. That is also what
//  lets a heading be dynamic: open() writes the document and the stylesheet,
//  the <h1> belongs to the page, so "RIDE 7" needs no flag. links() is the same
//  bargain for the navigation: a page that wants it in the body asks, and is not
//  given a second copy in the footer.
//
//  It knows no app. The front page is the base's own, in web/IndexPage.cpp: the
//  device's raw numbers and a button per page that registered itself. So a
//  firmware with every app deleted still answers at 192.168.4.1 with something
//  true, which is what the captive portal redirects every request to. It used to
//  be NOTES' form, and deleting that app took the root page with it.
//
//  The closing half happens in the destructor rather than at the end of every
//  page. A page that returns early, and the interesting ones all do, would
//  otherwise leave the document unterminated, and a chunked body with no final
//  empty chunk is a phone waiting out its own timeout on a finished page. See
//  WebOut.h, which flushes on the same principle.
// -----------------------------------------------------------------------------
#include "web/PageRegistry.h"
#include "web/WebOut.h"

#include <WebServer.h>

#include <cstdint>

namespace sd {

/// One request, being answered. Handed to a page; never held past the reply.
class Page {
public:
    Page(WebServer& srv, const PageInfo& info) : _srv(srv), _info(info), _out(srv) {}
    ~Page();

    Page(const Page&)            = delete;
    Page& operator=(const Page&) = delete;

    /// Begin an HTML document with the shared head, titled as the page is.
    void open() { open(_info.title); }
    /// The same, for a page whose title is only known once the request is read.
    void open(const char* title);
    bool opened() const { return _opened; }

    /// Write the console's navigation here, as buttons: every other registered
    /// page that named itself. A page that calls this gets no second copy in
    /// the footer, so the front page can BE the navigation while every other
    /// page keeps it small at the bottom.
    void links();

    WebOut&    out() { return _out; }
    WebServer& srv() { return _srv; }

    /// The `id` argument, or zero when there was not a usable one. Zero is not
    /// a record, the stores numbering from one, so it doubles as "no".
    uint32_t id() const;

    /// Answer a POST by pointing somewhere else. See Other rather than a page:
    /// a POST left in the history is an action the browser offers to repeat
    /// every time somebody hits reload.
    void seeOther(const char* path);

private:
    WebServer&      _srv;
    const PageInfo& _info;
    WebOut          _out;
    bool            _opened = false;
    bool            _linked = false;
};

class Console {
public:
    /// Port 80 so nothing has to be typed after the address.
    static constexpr uint16_t kPort = 80;

    /// Raises the server and the captive-portal DNS, and wires up every page
    /// that registered itself. The caller is whichever app raised the network.
    bool begin();
    void end();
    /// Must be called often. The server is synchronous, so a request waits
    /// exactly as long as the gap between calls.
    void poll();

    bool up() const { return _up; }

    /// Requests answered since the console came up.
    ///
    /// On the panel because it answers what somebody in front of the device
    /// actually asks: is the phone talking to this thing? A page that will not
    /// load and a phone that never joined look identical from here otherwise,
    /// and they are fixed differently.
    uint32_t served() const;

private:
    bool _up = false;
};

Console& console();

}  // namespace sd
