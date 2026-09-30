#include "services/Settings.h"

#include "core/Log.h"

#include <nvs.h>

#include <cstring>

namespace sd {

namespace {
/// Whether a namespace has ever been written, asked quietly.
///
/// A read-only open of a namespace nobody has stored anything in yet fails, and
/// Preferences::begin() logs that at ERROR level: `nvs_open failed: NOT_FOUND`
/// on every boot until the first calibration is entered. That is the ordinary
/// state of a store before anything is put in it, not a fault, and a log full of
/// ordinary states is where a real error goes to hide - the same reason has()
/// reaches past Preferences for its own check.
///
/// nvs_open itself is quiet; only Preferences' wrapper logs. So ask it directly
/// and let a missing namespace be the answer rather than an error.
bool namespaceExists(const char* ns) {
    nvs_handle_t    h   = 0;
    const esp_err_t err = nvs_open(ns, NVS_READONLY, &h);
    if (err != ESP_OK) return false;
    nvs_close(h);
    return true;
}
}  // namespace

Settings::Settings(const char* ns, bool readOnly) {
    char safe[16];
    std::strncpy(safe, ns ? ns : "default", sizeof(safe) - 1);
    safe[sizeof(safe) - 1] = '\0';

    // Nothing stored yet is not a failure to report. A read-write open still
    // goes straight through: that one creates the namespace, so if it cannot
    // open there is something actually wrong and it is worth a line.
    if (readOnly && !namespaceExists(safe)) return;

    _ok = _prefs.begin(safe, readOnly);
    if (!_ok) SD_LOGW("settings", "could not open namespace '%s'", safe);
}

Settings::~Settings() {
    if (_ok) _prefs.end();
}

bool Settings::has(const char* key) const { return _ok && _prefs.isKey(key); }

// Guarded rather than trusted to the default. Every one of these logs an error
// of its own for a key that has never been written, which is the state every
// setting starts in.
float   Settings::getFloat(const char* k, float def) const   { return has(k) ? _prefs.getFloat(k, def) : def; }
int32_t Settings::getInt(const char* k, int32_t def) const   { return has(k) ? _prefs.getInt(k, def) : def; }
bool    Settings::getBool(const char* k, bool def) const     { return has(k) ? _prefs.getBool(k, def) : def; }

namespace {
/// Every Preferences setter returns the number of bytes it wrote, and zero for
/// every kind of refusal, a full partition among them. One message for all of
/// them, so a store that refused says so once and in the same words.
bool stored(const char* key, size_t wrote, size_t want) {
    if (wrote == want) return true;
    SD_LOGW("settings", "'%s' (%u bytes) not stored - nvs full?", key,
            static_cast<unsigned>(want));
    return false;
}
}  // namespace

// A bool is stored as one byte, not as sizeof(bool): Preferences puts it
// through putUChar.
bool Settings::putFloat(const char* k, float v) { return _ok && stored(k, _prefs.putFloat(k, v), sizeof(float)); }
bool Settings::putInt(const char* k, int32_t v) { return _ok && stored(k, _prefs.putInt(k, v), sizeof(int32_t)); }
bool Settings::putBool(const char* k, bool v)   { return _ok && stored(k, _prefs.putBool(k, v), 1); }

bool Settings::getBlob(const char* key, void* out, size_t len) const {
    if (!has(key)) return false;
    if (_prefs.getBytesLength(key) != len) return false;
    return _prefs.getBytes(key, out, len) == len;
}

bool Settings::putBlob(const char* key, const void* in, size_t len) {
    return _ok && stored(key, _prefs.putBytes(key, in, len), len);
}

void Settings::remove(const char* key) { if (_ok) _prefs.remove(key); }
void Settings::clearAll()              { if (_ok) _prefs.clear(); }

}  // namespace sd
