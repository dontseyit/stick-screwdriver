#pragma once
// -----------------------------------------------------------------------------
//  MoodService - the base's idle timer.
//
//  The framework keeps the time since the last press and says when an app
//  opens, closes or is used. In the full firmware something listens to all of
//  that and models how the device is doing; here the timer is the whole of it,
//  because the timer is all the base reads back. AppManager dims the panel from
//  idleSec() and nothing else asks this anything.
//
//  Keep the signatures. An app written against the full firmware compiles here
//  unchanged, and dropping a fuller implementation over this file is the whole
//  of installing one.
// -----------------------------------------------------------------------------
#include "core/Event.h"

#include <cstdint>

namespace sd {

class MoodService {
public:
    void begin();

    void tick(uint32_t nowMs) { (void)nowMs; }

    void noteInput(const InputEvent& ev);
    void noteUse() {}
    void noteAppStart(const char* id, const char* title) { (void)id; (void)title; }
    void noteAppStop() {}

    /// Seconds since the last input. The panel's dim timer reads this.
    uint32_t idleSec() const;

private:
    uint32_t _lastInputMs = 0;
};

}  // namespace sd
