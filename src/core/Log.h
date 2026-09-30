#pragma once
// -----------------------------------------------------------------------------
//  Tiny levelled logger. Calls above SD_LOG_LEVEL compile away to nothing, so
//  leaving diagnostics in an app costs nothing in a release build.
// -----------------------------------------------------------------------------
#include <Arduino.h>

#ifndef SD_LOG_LEVEL
#define SD_LOG_LEVEL 3
#endif

namespace sd {
void logPrintf(char level, const char* tag, const char* fmt, ...)
    __attribute__((format(printf, 3, 4)));
}

#define SD_LOG_AT(lvl, ch, tag, ...) \
    do { if (SD_LOG_LEVEL >= (lvl)) ::sd::logPrintf(ch, tag, __VA_ARGS__); } while (0)

#define SD_LOGE(tag, ...) SD_LOG_AT(1, 'E', tag, __VA_ARGS__)
#define SD_LOGW(tag, ...) SD_LOG_AT(2, 'W', tag, __VA_ARGS__)
#define SD_LOGI(tag, ...) SD_LOG_AT(3, 'I', tag, __VA_ARGS__)
#define SD_LOGD(tag, ...) SD_LOG_AT(4, 'D', tag, __VA_ARGS__)
#define SD_LOGV(tag, ...) SD_LOG_AT(5, 'V', tag, __VA_ARGS__)
