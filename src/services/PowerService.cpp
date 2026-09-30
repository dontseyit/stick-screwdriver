#include "services/PowerService.h"

#include "core/Log.h"

#include <M5Unified.h>

namespace sd {

void PowerService::begin() {
    _lastMs = 0;
    refresh(millis());
}

void PowerService::refresh(uint32_t nowMs) {
    if (_lastMs != 0 && (nowMs - _lastMs) < 1000) return;
    _lastMs   = nowMs;
    _pct      = M5.Power.getBatteryLevel();
    _mv       = M5.Power.getBatteryVoltage();
    _charging = M5.Power.isCharging() == m5::Power_Class::is_charging;
}

float PowerService::ext5vInVolts() {
    // The mask is ignored on this board; the PMIC has one 5 V node.
    const float mv = M5.Power.getExtVoltage(static_cast<m5::ext_port_mask_t>(0xFF));
    return (mv > 0.0f) ? (mv / 1000.0f) : 0.0f;
}

bool PowerService::ext5vBoostOn() { return M5.Power.getExtOutput(); }

void PowerService::acquireExt5V() {
    if (_ext5vRefs++ == 0) {
        M5.Power.setExtOutput(true);
        SD_LOGI("power", "EXT_5V on");
    }
}

void PowerService::releaseExt5V() {
    if (_ext5vRefs > 0 && --_ext5vRefs == 0) {
        M5.Power.setExtOutput(false);
        SD_LOGI("power", "EXT_5V off");
    }
}

}  // namespace sd
