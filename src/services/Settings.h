#pragma once
// -----------------------------------------------------------------------------
//  Settings - namespaced persistence on top of NVS.
//
//  Each app gets its own namespace keyed by its AppInfo::id, so two apps cannot
//  collide on a key name and removing an app cannot corrupt another's state.
//  Calibration data lives here, which is why a level stays calibrated across a
//  battery pull.
// -----------------------------------------------------------------------------
#include <Preferences.h>

#include <cstddef>
#include <cstdint>

namespace sd {

class Settings {
public:
    /// `ns` is truncated to the 15 characters NVS allows.
    Settings(const char* ns, bool readOnly);
    ~Settings();

    bool ok() const { return _ok; }

    /// Whether anything has been stored under `key`, without complaining when
    /// nothing has.
    ///
    /// Preferences logs at ERROR level for a key that was never written:
    /// `nvs_get_blob len fail: warmup NOT_FOUND` on every boot until the
    /// thermal model has seen its first cold start. A setting that has not been
    /// set is the ordinary state of a setting, and a log full of ordinary
    /// states is where a real error hides. isKey() reaches the raw nvs_get_*
    /// calls, which are quiet.
    bool has(const char* key) const;

    float   getFloat(const char* key, float def) const;
    int32_t getInt(const char* key, int32_t def) const;
    bool    getBool(const char* key, bool def) const;

    /// False when the write did not happen, and it logs why.
    ///
    /// A store that cannot say it did not store something is not a store. A
    /// full NVS refuses every write on the device at once, a night's chart, a
    /// mood, a calibration, and the only trace is the Arduino core's own
    /// `nvs_set_* fail` line in a serial log nobody is reading. Checking the
    /// result is the caller's choice; being able to is not.
    bool putFloat(const char* key, float v);
    bool putInt(const char* key, int32_t v);
    bool putBool(const char* key, bool v);

    /// Fixed-size record storage. `getBlob` leaves `out` untouched and returns
    /// false unless a record of exactly `len` bytes is present, so a struct that
    /// changes shape is ignored rather than misread.
    bool getBlob(const char* key, void* out, size_t len) const;

    /// False when the write did not happen, and it logs why; see the setters
    /// above for what that is worth.
    bool putBlob(const char* key, const void* in, size_t len);

    void remove(const char* key);
    void clearAll();

private:
    mutable Preferences _prefs;
    bool                _ok = false;
};

}  // namespace sd
