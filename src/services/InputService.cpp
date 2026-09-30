#include "services/InputService.h"

#include <M5Unified.h>

namespace sd {

void InputService::begin() { _grammar.flush(); }

void InputService::poll(uint32_t now) {
    _grammar.poll(M5.BtnA.isPressed(), M5.BtnB.isPressed(), M5.BtnPWR.isPressed(), now);
}

}  // namespace sd
