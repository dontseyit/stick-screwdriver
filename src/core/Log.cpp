#include "core/Log.h"

#include <cstdarg>
#include <cstdio>

namespace sd {

void logPrintf(char level, const char* tag, const char* fmt, ...) {
    char    buf[192];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    Serial.printf("[%8lu][%c][%s] %s\n",
                  static_cast<unsigned long>(millis()), level, tag, buf);
}

}  // namespace sd
