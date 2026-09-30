#include "mathx/WifiQr.h"

#include <cstring>

namespace sd {

namespace {

/// The five the format gives meaning to.
bool needsEscape(char c) {
    return c == '\\' || c == ';' || c == ',' || c == ':' || c == '"';
}

/// Appends `s` escaped. False when it would not fit, leaving `w` where it was
/// so the caller can abandon the whole payload rather than emit a short one.
bool put(const char* s, char* out, size_t cap, size_t& w) {
    for (const char* p = s; *p; ++p) {
        const size_t need = needsEscape(*p) ? 2u : 1u;
        if (w + need + 1 > cap) return false;
        if (need == 2) out[w++] = '\\';
        out[w++] = *p;
    }
    return true;
}

bool putLit(const char* s, char* out, size_t cap, size_t& w) {
    const size_t n = std::strlen(s);
    if (w + n + 1 > cap) return false;
    std::memcpy(out + w, s, n);
    w += n;
    return true;
}

}  // namespace

size_t wifiQrPayload(const char* ssid, const char* pass, char* out, size_t cap) {
    if (!out || cap == 0) return 0;
    out[0] = '\0';
    if (!ssid || !*ssid) return 0;

    // The same test RadioService::beginAp makes. Both read the key rather than
    // a flag, so there is no state that can be right in one and stale in the
    // other.
    const bool secured = pass && std::strlen(pass) >= 8;

    size_t w = 0;
    bool   ok = putLit(secured ? "WIFI:T:WPA;S:" : "WIFI:T:nopass;S:", out, cap, w);
    ok = ok && put(ssid, out, cap, w);
    if (secured) {
        ok = ok && putLit(";P:", out, cap, w);
        ok = ok && put(pass, out, cap, w);
    }
    // Two, and both are needed: the first ends the last field and the second
    // ends the record. Not all readers tolerate one missing.
    ok = ok && putLit(";;", out, cap, w);

    if (!ok) { out[0] = '\0'; return 0; }
    out[w] = '\0';
    return w;
}

}  // namespace sd
