#include "web/PageRegistry.h"

#include <cstring>

namespace sd {

PageRegistry& PageRegistry::instance() {
    static PageRegistry reg;  // constructed on first use, before any registrar
    return reg;
}

bool PageRegistry::add(const PageInfo& info) {
    if (_count >= kMaxPages) return false;
    if (!info.path || !info.fn) return false;
    if (info.path[0] != '/') return false;
    if (find(info.path, info.method)) return false;
    _pages[_count++] = &info;
    return true;
}

const PageInfo* PageRegistry::find(const char* path, Method m) const {
    if (!path) return nullptr;
    for (size_t i = 0; i < _count; ++i) {
        if (_pages[i]->method == m && std::strcmp(_pages[i]->path, path) == 0) {
            return _pages[i];
        }
    }
    return nullptr;
}

int PageRegistry::navFor(const char* path, const PageInfo** out, int max) const {
    if (!out || max <= 0) return 0;
    int n = 0;
    for (size_t i = 0; i < _count && n < max; ++i) {
        const PageInfo* p = _pages[i];
        if (!p->nav) continue;
        // A page does not link to itself. Compared by path rather than by
        // pointer so that a POST sharing a path with its GET - /add posting
        // back to / - does not put the page it returns to in its own footer.
        if (path && std::strcmp(p->path, path) == 0) continue;
        out[n++] = p;
    }
    return n;
}

PageRegistrar::PageRegistrar(const PageInfo& info) {
    PageRegistry::instance().add(info);
}

}  // namespace sd
