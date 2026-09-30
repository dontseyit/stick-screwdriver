#include "services/ServiceHub.h"

#include "services/Calibration.h"
#include "services/SystemSettings.h"

#include "core/Log.h"

#include <M5Unified.h>

namespace sd {

ServiceHub& ServiceHub::instance() {
    static ServiceHub hub;
    return hub;
}

void ServiceHub::begin() {
    // First of all: the thermal model reads its trim out of here on the way up,
    // and anything wanting an absolute number from a sensor reads it later.
    Calibration::instance().begin();
    // And the device's own preferences, which expandImplied() consults on every
    // request from here on, so before the first app can make one.
    SystemSettings::instance().begin();

    _input.begin();
    _power.begin();
    // Start from a known state rather than an assumed one. `_active = 0` says
    // "nothing has been acquired", which is not "nothing is on": the speaker
    // amplifier is a latched bit in the PMIC and survives a reset, so a
    // previous session that played one beep would leave it enabled here, and IR
    // reception does not work while it is.
    _audio.forceOff();
    _active = 0;

    // Before the mood, which reads the air it estimates. Owns no capability
    // either: all three of its sensors are readable whatever is acquired.
    _thermal.begin();

    // Last, because it appraises the battery on the way up and wants a reading
    // rather than a -1. It owns no hardware, so it never appears in a Cap mask
    // and never has to be acquired.
    _mood.begin();
}

void ServiceHub::acquire(Cap c) {
    switch (c) {
        case Cap::Imu:     _imu.begin(); break;
        case Cap::Ext5V:   _power.acquireExt5V(); break;
        case Cap::Speaker: _audio.begin(); break;
        case Cap::Mic:     _mic.begin(_mic.sampleRate(), _mic.magnification()); break;
        case Cap::IrRx:
            // Asserted, not assumed. expandImplied() clears the Speaker bit,
            // but clearing a bit that was never set releases nothing, and the
            // amplifier may have been left on by an entirely different boot.
            _audio.forceOff();
            _ir.begin();
            break;
        case Cap::IrTx:    _ir.beginTx(); break;
        case Cap::PortA:   _port.begin(); break;
        case Cap::WiFi:    _radio.beginWifi(); break;
        case Cap::Ble:     _radio.beginBle(); break;
        default: break;  // Display is always on; radios arrive with their apps
    }
}

void ServiceHub::release(Cap c) {
    switch (c) {
        case Cap::Imu:     _imu.end(); break;
        case Cap::Ext5V:   _power.releaseExt5V(); break;
        case Cap::Speaker: _audio.end(); break;
        case Cap::Mic:     _mic.end(); break;
        case Cap::IrRx:    _ir.end(); break;
        case Cap::IrTx:    _ir.endTx(); break;
        case Cap::PortA:   _port.end(); break;
        case Cap::WiFi:
        case Cap::Ble:     _radio.end(); break;
        default: break;
    }
}

void ServiceHub::applyCaps(CapMask want) {
    want = expandImplied(want, systemSettings().libraryMode());
    if (want == _active) return;

    const CapMask added   = want & ~_active;
    const CapMask removed = _active & ~want;

    // Release first: freeing a conflicting peripheral before claiming its
    // replacement is what makes the IR/speaker rule hold.
    for (uint32_t bit = 0; bit < 32; ++bit) {
        const CapMask m = 1u << bit;
        if ((removed & m) && m != caps(Cap::Ext5V)) release(static_cast<Cap>(m));
    }
    // The 5V rail is special-cased at both ends, because bit order is not
    // dependency order: the IR receiver sits on this rail, and a receiver
    // created before its supply arrives spends its first moments watching a
    // floating pin. Power up before, power down after.
    if (added & caps(Cap::Ext5V)) acquire(Cap::Ext5V);
    for (uint32_t bit = 0; bit < 32; ++bit) {
        const CapMask m = 1u << bit;
        if ((added & m) && m != caps(Cap::Ext5V)) acquire(static_cast<Cap>(m));
    }
    if (removed & caps(Cap::Ext5V)) release(Cap::Ext5V);

    _active = want;
    SD_LOGD("hub", "caps now 0x%03x", static_cast<unsigned>(_active));
}

}  // namespace sd
