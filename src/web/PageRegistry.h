#pragma once
// -----------------------------------------------------------------------------
//  PageRegistry - the set of pages the web console serves, populated at
//  static-init time by SD_REGISTER_PAGE.
//
//  An app is added to this device by writing one translation unit and putting
//  SD_REGISTER_APP at the bottom of it. Nothing else is edited, and removing
//  the app is removing the file. The web console had the opposite: a page meant
//  an include, a route, a handler body, a handler declaration, and a
//  hand-written link in however many footers somebody remembered. Five edits,
//  all in a file belonging to NOTES, for a page that was not about notes.
//
//  It cost what that always costs. PLACE was removed and its five routes had to
//  be found by hand in another app's file; the footers were lists nothing kept
//  true; and the stylesheet had been copied once and drifted.
//
//  So pages register themselves, exactly as apps do, and everything that used
//  to be a list is derived from the registry. Deleting a feature's file deletes
//  its routes and its place in every other page's footer with it.
//
//  There is no Arduino in this header, on purpose. Which of two paths a request
//  matches, whether a path was registered twice, and which links belong in a
//  footer are all questions about a list, and a list can be tested on a laptop.
//  Everything that needs a socket lives in web/Console.h, so this half compiles
//  into the host tests; see test/test_web. That is why Method is an enum of our
//  own rather than HTTPMethod, and why Page is only forward-declared: a pointer
//  to a function taking an incomplete type needs nothing but its name.
// -----------------------------------------------------------------------------
#include <cstddef>
#include <cstdint>

namespace sd {

class Page;

/// How a page is asked for. Ours rather than the server's, so this header costs
/// nothing to include and compiles without an Arduino.
enum class Method : uint8_t { Get, Post };

/// What a page does when it is asked for. It is handed the request and decides
/// whether to open a document at all; see Page in web/Console.h.
using PageFn = void (*)(Page&);

struct PageInfo {
    /// The path, with its leading slash. Two pages may share one path only if
    /// their methods differ, which is how a form posts to the page it came from.
    const char* path = nullptr;
    Method      method = Method::Get;

    /// The document title. A page free to write whatever heading it likes still
    /// wants a fixed name in the browser's tab and in everybody else's footer.
    const char* title = nullptr;

    /// How this page reads as a link in every OTHER page's footer. Null keeps
    /// it out of the navigation, which is what a picture or a CSV wants: those
    /// are reached from a page, never from a list of places to go.
    const char* nav = nullptr;

    /// What the page says about itself, written out between the body and the
    /// links exactly as it stands, so it is HTML and prose wanting two
    /// paragraphs simply is two. Null for a page that needs no explaining.
    const char* note = nullptr;

    /// Rules only this page needs, written into the console's own <style> after
    /// the frame's. Null for a page dressed entirely in what the frame offers.
    ///
    /// A rule TWO pages use belongs in the frame instead, never copied into
    /// both; see kStyle in web/Console.cpp for what happened the last time one
    /// design was written down twice.
    const char* css = nullptr;

    PageFn fn = nullptr;
};

/// Constructing one of these at namespace scope adds the page to the registry.
struct PageRegistrar {
    explicit PageRegistrar(const PageInfo& info);
};

class PageRegistry {
public:
    static constexpr size_t kMaxPages = 24;

    static PageRegistry& instance();

    /// Refuses a page with no path or no function, a second page with the same
    /// path AND method, and anything past kMaxPages. Refusing rather than
    /// overwriting matters: two pages claiming one route is a mistake somebody
    /// has to see, and the winner would otherwise depend on link order.
    bool add(const PageInfo& info);

    size_t          count() const { return _count; }
    const PageInfo& at(size_t i) const { return *_pages[i]; }

    /// The page for a request, or null. Method is part of the question because
    /// GET / and POST /add are different pages that may share a path.
    const PageInfo* find(const char* path, Method m) const;

    /// The links that belong in `path`'s footer: every registered page with a
    /// nav label, except the one asking. Returns how many were written.
    ///
    /// Derived rather than declared, which is the point: a page that is deleted
    /// stops appearing in every other footer with nobody remembering to look.
    int navFor(const char* path, const PageInfo** out, int max) const;

    /// Only the tests need this: a test that could not start from empty would
    /// be a test of whatever ran before it.
    void clear() { _count = 0; }

private:
    PageRegistry() = default;

    const PageInfo* _pages[kMaxPages]{};
    size_t          _count = 0;
};

/// Place at the bottom of a page's .cpp, next to its `kPage`.
#define SD_REGISTER_PAGE(Name, INFO) \
    static const ::sd::PageRegistrar Name##_registrar{(INFO)};

}  // namespace sd
