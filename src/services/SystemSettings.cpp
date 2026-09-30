#include "services/SystemSettings.h"

#include "core/Log.h"
#include "services/Settings.h"

namespace sd {
namespace {

/// Not any app's id, and no app may take it: an app's namespace goes when the
/// app does, and these have to outlive every one of them.
constexpr const char* kNs = "device";

constexpr const char* kLibraryKey = "library";

}  // namespace

SystemSettings& SystemSettings::instance() {
    static SystemSettings s;
    return s;
}

void SystemSettings::begin() {
    Settings s(kNs, /*readOnly*/ true);
    if (!s.ok()) return;
    _libraryMode = s.getBool(kLibraryKey, false);
    if (_libraryMode) SD_LOGI("device", "library mode on - speaker withheld");
}

void SystemSettings::setLibraryMode(bool on) {
    if (on == _libraryMode) return;
    _libraryMode = on;
    // Written now rather than on some exit path: a preference is set once and
    // expected to hold, and no later moment is more certain to happen.
    Settings s(kNs, /*readOnly*/ false);
    if (s.ok()) s.putBool(kLibraryKey, on);
    SD_LOGI("device", "library mode %s", on ? "on" : "off");
}

}  // namespace sd
