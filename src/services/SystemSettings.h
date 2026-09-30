#pragma once
// -----------------------------------------------------------------------------
//  SystemSettings - the preferences that belong to the device, not to a tool.
//
//  Every app keeps its own settings under its own AppInfo::id, which is the
//  right home for a value one tool owns and the wrong home for a decision every
//  tool has to obey: a tool cannot be asked to look in another tool's
//  namespace, and removing that other tool would take the setting with it. So
//  the device has a namespace of its own, loaded once at boot before any app
//  can ask for hardware.
//
//  Library Mode is the first of them: every sound off, in every tool. It is not
//  enforced here. ServiceHub strikes Cap::Speaker from every request while it
//  is on, beside the rules that already say IR and the microphone win over the
//  speaker, so the amplifier is never powered rather than powered and told to
//  keep quiet.
//
//  Adding a second preference is a field, a key, and a row on SYSTEM's SETTINGS
//  page.
// -----------------------------------------------------------------------------

namespace sd {

class SystemSettings {
public:
    static SystemSettings& instance();

    /// Loads every preference. Called once from ServiceHub::begin(), before the
    /// first capability request.
    void begin();

    /// Every sound off, everywhere, until switched back.
    bool libraryMode() const { return _libraryMode; }
    /// Takes effect on the next capability request and survives a reboot.
    void setLibraryMode(bool on);

private:
    SystemSettings() = default;

    bool _libraryMode = false;
};

inline SystemSettings& systemSettings() { return SystemSettings::instance(); }

}  // namespace sd
