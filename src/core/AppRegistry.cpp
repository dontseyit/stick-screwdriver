#include "core/AppRegistry.h"

#include <cstring>

namespace sd {

AppRegistry& AppRegistry::instance() {
    static AppRegistry reg;  // constructed on first use, before any registrar
    return reg;
}

bool AppRegistry::add(const AppInfo& info, AppFactory factory) {
    if (_count >= kMaxApps || factory == nullptr) return false;
    _entries[_count++] = AppEntry{&info, factory};
    return true;
}

const AppEntry* AppRegistry::find(const char* id) const {
    if (!id) return nullptr;
    for (size_t i = 0; i < _count; ++i) {
        if (std::strcmp(_entries[i].info->id, id) == 0) return &_entries[i];
    }
    return nullptr;
}

void AppRegistry::sort() {
    // Pinned first, then by title. A sort key has to be something the reader
    // can see, and a pin is: it is the row at the top. Group is not drawn, so
    // sorting by it put RIDE between LEVEL and ROBOT JIG for no visible reason.
    //
    // Insertion sort: n is tiny and it is stable, so apps sharing a title or a
    // pin stay in registration order.
    auto before = [](const AppInfo& a, const AppInfo& b) {
        if (a.pinned != b.pinned) return a.pinned;
        return std::strcmp(a.title, b.title) <= 0;
    };

    for (size_t i = 1; i < _count; ++i) {
        const AppEntry key = _entries[i];
        size_t         j   = i;
        while (j > 0) {
            const AppEntry& prev = _entries[j - 1];
            if (before(*prev.info, *key.info)) break;
            _entries[j] = prev;
            --j;
        }
        _entries[j] = key;
    }
}

AppRegistrar::AppRegistrar(const AppInfo& info, AppFactory factory) {
    AppRegistry::instance().add(info, factory);
}

}  // namespace sd
