#pragma once
// -----------------------------------------------------------------------------
//  The set of apps built into this firmware, populated at static-init time by
//  SD_REGISTER_APP. Storage lives in a function-local static, so it exists
//  before the first registrar runs whatever order the linker chose.
// -----------------------------------------------------------------------------
#include "core/App.h"

#include <cstddef>

namespace sd {

class AppRegistry {
public:
    static constexpr size_t kMaxApps = 48;

    static AppRegistry& instance();

    bool add(const AppInfo& info, AppFactory factory);

    size_t          count() const { return _count; }
    const AppEntry& at(size_t i) const { return _entries[i]; }
    const AppEntry* find(const char* id) const;

    /// Stable sort by title, so launcher order does not depend on link order.
    /// Title alone, not group then title: a grouped list means knowing which
    /// group a tool is in before you can find it.
    void sort();

    /// Only the tests need this: a test that could not start from empty would
    /// be a test of whatever ran before it.
    void clear() { _count = 0; }

private:
    AppRegistry() = default;

    AppEntry _entries[kMaxApps]{};
    size_t   _count = 0;
};

}  // namespace sd
