#include "services/MoodService.h"

#include <Arduino.h>

namespace sd {

// From boot rather than from zero: idleSec() is asked before the first press,
// and a device that has just started has not been idle since the epoch.
void MoodService::begin() { _lastInputMs = millis(); }

void MoodService::noteInput(const InputEvent& ev) {
    (void)ev;
    _lastInputMs = millis();
}

uint32_t MoodService::idleSec() const { return (millis() - _lastInputMs) / 1000u; }

}  // namespace sd
