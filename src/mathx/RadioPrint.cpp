#include "mathx/RadioPrint.h"

#include <cmath>

namespace sd {
namespace {

float clampf(float v, float lo, float hi) {
    return (v < lo) ? lo : (v > hi) ? hi : v;
}

/// How far below a capture's strongest reading an access point has to be before
/// it is worth almost nothing. Forty decibels is roughly a scan's useful span:
/// the router in the room against the one three streets away.
constexpr float kWeightSpanDb = 40.0f;
constexpr float kFloorW       = 0.05f;

}  // namespace

// -----------------------------------------------------------------------------
uint32_t bssidHash(const uint8_t* mac) {
    if (!mac) return 0;
    // FNV-1a. Cheap, well mixed over six bytes, and the same on any machine
    // that reads a stored capture back.
    uint32_t h = 2166136261u;
    for (int i = 0; i < 6; ++i) {
        h ^= mac[i];
        h *= 16777619u;
    }
    // Zero is the empty slot in a print, so it must not be a legal name.
    return h ? h : 1u;
}

bool bleAddressIsPrivate(bool randomType, uint8_t addrMsb) {
    // A public address is never private however its bytes fall, and that half
    // of the test is the whole point of the function existing.
    return randomType && (addrMsb & 0xC0u) == 0x40u;
}

int orderCsiCandidates(const int8_t* rssi, const bool* isVirtual, int n,
                       uint8_t* order, int cap) {
    if (!rssi || !isVirtual || !order) return 0;
    int count = 0;
    for (int pass = 0; pass < 2; ++pass) {
        // Where this pass began. The insertion below never crosses it, which is
        // what keeps the two groups apart however loud a virtual BSSID is.
        const int from = count;
        for (int i = 0; i < n && count < cap; ++i) {
            if (isVirtual[i] != (pass == 1)) continue;
            // Strictly less, so an equal level does not displace the one
            // already there: the scan's order survives as the tiebreak.
            int at = count;
            while (at > from && rssi[order[at - 1]] < rssi[i]) {
                order[at] = order[at - 1];
                --at;
            }
            order[at] = static_cast<uint8_t>(i);
            ++count;
        }
    }
    return count;
}

bool wifiAddressIsVirtual(uint8_t macMsb) {
    // Bit 1, the locally administered bit. Bit 0 is the group bit and means a
    // multicast address, which a BSSID is not: testing the low nibble would
    // call every second real radio virtual.
    return (macMsb & 0x02u) != 0u;
}

float radioWeight(int rssiDbm, int referenceDbm) {
    const float below = static_cast<float>(referenceDbm - rssiDbm);
    return clampf(1.0f - below / kWeightSpanDb, kFloorW, 1.0f);
}

int radioReferenceDbm(const RadioPrint& p) {
    if (p.count <= 0) return -127;

    // The best three, by letting each reading displace the first one it beats
    // and pushing that one along. The print is sorted by name, not by level, so
    // they arrive in no useful order.
    int top[kRadioTopForRef];
    for (int& t : top) t = -127;
    for (int i = 0; i < p.count; ++i) {
        int v = p.rssi[i];
        for (int k = 0; k < kRadioTopForRef; ++k) {
            if (v > top[k]) {
                const int held = top[k];
                top[k]         = v;
                v              = held;
            }
        }
    }

    const int n = (p.count < kRadioTopForRef) ? p.count : kRadioTopForRef;
    int       sum = 0;
    for (int k = 0; k < n; ++k) sum += top[k];
    return sum / n;
}

const char* radioVerdictName(RadioVerdict v) {
    switch (v) {
        case RadioVerdict::Unmeasured:  return "no wifi seen";
        case RadioVerdict::NoReference: return "nothing learned";
        case RadioVerdict::TooFew:      return "needs more visits";
        case RadioVerdict::Match:       return "this room";
        case RadioVerdict::Unsure:      return "unsure";
        case RadioVerdict::Different:   return "somewhere else";
        default:                        return "?";
    }
}

const char* radioAspectName(RadioAspect a) {
    switch (a) {
        case RadioAspect::Set:    return "wifi";
        case RadioAspect::Signal: return "level";
        default:                  return "?";
    }
}

// -----------------------------------------------------------------------------
bool buildRadioPrint(const uint32_t* ids, const int8_t* rssi, int n,
                     RadioPrint& out) {
    out = RadioPrint{};
    if (!ids || !rssi || n <= 0) return false;

    // Keep the strongest, by selection: n is at most a couple of dozen and this
    // needs no scratch buffer.
    int taken[kRadioMaxAps];
    int got = 0;
    for (int slot = 0; slot < kRadioMaxAps && slot < n; ++slot) {
        int best = -1;
        for (int i = 0; i < n; ++i) {
            if (ids[i] == 0) continue;
            bool used = false;
            for (int k = 0; k < got; ++k) {
                if (taken[k] == i) { used = true; break; }
            }
            if (used) continue;
            // A duplicate BSSID in the input would otherwise occupy two slots
            // and be counted twice by every comparison below.
            bool dup = false;
            for (int k = 0; k < got; ++k) {
                if (ids[taken[k]] == ids[i]) { dup = true; break; }
            }
            if (dup) continue;
            if (best < 0 || rssi[i] > rssi[best]) best = i;
        }
        if (best < 0) break;
        taken[got++] = best;
    }
    if (got < kRadioMinAps) return false;

    // Sorted by name, so two prints can be walked in step rather than searched.
    for (int i = 1; i < got; ++i) {
        const int key = taken[i];
        int       j   = i;
        while (j > 0 && ids[taken[j - 1]] > ids[key]) {
            taken[j] = taken[j - 1];
            --j;
        }
        taken[j] = key;
    }

    for (int i = 0; i < got; ++i) {
        out.id[i]   = ids[taken[i]];
        out.rssi[i] = rssi[taken[i]];
    }
    out.count = static_cast<uint8_t>(got);
    out.valid = true;
    return true;
}

// -----------------------------------------------------------------------------
float radioSetSimilarity(const RadioPrint& a, const RadioPrint& b) {
    if (!a.valid || !b.valid) return 0.0f;

    // Each capture's weights are measured against its OWN strongest reading, so
    // a scan taken through a body is compared on which access points mattered
    // rather than on how loud they happened to be.
    const int sa = radioReferenceDbm(a);
    const int sb = radioReferenceDbm(b);

    double lo = 0.0, hi = 0.0;
    int    i = 0, j = 0;
    while (i < a.count || j < b.count) {
        if (i < a.count && (j >= b.count || a.id[i] < b.id[j])) {
            hi += static_cast<double>(radioWeight(a.rssi[i], sa));  // only a

            ++i;
        } else if (j < b.count && (i >= a.count || b.id[j] < a.id[i])) {
            hi += static_cast<double>(radioWeight(b.rssi[j], sb));  // only b

            ++j;
        } else {
            const float wa = radioWeight(a.rssi[i], sa);
            const float wb = radioWeight(b.rssi[j], sb);
            lo += static_cast<double>((wa < wb) ? wa : wb);
            hi += static_cast<double>((wa > wb) ? wa : wb);
            ++i;
            ++j;
        }
    }
    return (hi > 1e-9) ? static_cast<float>(lo / hi) : 0.0f;
}

float radioSignalDistance(const RadioPrint& a, const RadioPrint& b, int* shared) {
    int n = 0;
    if (shared) *shared = 0;
    if (!a.valid || !b.valid) return 0.0f;

    // Two passes. The first finds the shared access points and each capture's
    // mean over exactly those; the second measures how far the two patterns
    // differ once that mean is gone. The mean has to be over the shared set: an
    // access point only one capture saw would otherwise shift that capture's
    // mean and tilt every difference with it.
    const int sa = radioReferenceDbm(a);
    const int sb = radioReferenceDbm(b);

    double meanA = 0.0, meanB = 0.0;
    int    i = 0, j = 0;
    while (i < a.count && j < b.count) {
        if (a.id[i] < b.id[j]) { ++i; continue; }
        if (b.id[j] < a.id[i]) { ++j; continue; }
        meanA += a.rssi[i];
        meanB += b.rssi[j];
        ++n;
        ++i;
        ++j;
    }
    if (shared) *shared = n;
    if (n < kRadioMinShared) return 0.0f;
    meanA /= n;
    meanB /= n;

    double num = 0.0, den = 0.0;
    i = j = 0;
    while (i < a.count && j < b.count) {
        if (a.id[i] < b.id[j]) { ++i; continue; }
        if (b.id[j] < a.id[i]) { ++j; continue; }

        // Weighted by the WEAKER of the two: a level read near the noise floor
        // is the noisy one and should not dominate.
        const float wa = radioWeight(a.rssi[i], sa);
        const float wb = radioWeight(b.rssi[j], sb);
        const float w  = (wa < wb) ? wa : wb;
        // Both patterns with their own means already out, so what is left is
        // how differently the two capture the same set of routers.
        const double d = (static_cast<double>(a.rssi[i]) - meanA) -
                         (static_cast<double>(b.rssi[j]) - meanB);
        num += static_cast<double>(w) * d * d;
        den += static_cast<double>(w);
        ++i;
        ++j;
    }
    if (den <= 1e-9) return 0.0f;
    return static_cast<float>(std::sqrt(num / den));
}

// -----------------------------------------------------------------------------
void RadioReference::clear() {
    for (auto& c : _captures) c = RadioPrint{};
    _n = 0;
}

bool RadioReference::add(const RadioPrint& p) {
    if (!p.valid) return false;
    if (_n < kMaxCaptures) {
        _captures[_n++] = p;
        return true;
    }
    // Beyond the limit the oldest goes, so a place can be refreshed without
    // being forgotten first.
    for (int i = 1; i < kMaxCaptures; ++i) _captures[i - 1] = _captures[i];
    _captures[kMaxCaptures - 1] = p;
    return true;
}

const RadioPrint& RadioReference::at(int i) const {
    static const RadioPrint kEmpty{};
    return (i >= 0 && i < _n) ? _captures[i] : kEmpty;
}

float RadioReference::measure(RadioAspect a, const RadioPrint& x,
                              const RadioPrint& y, bool* usable) {
    if (a == RadioAspect::Set) {
        if (usable) *usable = x.valid && y.valid;
        return radioSetSimilarity(x, y);
    }
    int         n = 0;
    const float d = radioSignalDistance(x, y, &n);
    if (usable) *usable = n >= kRadioMinShared;
    return d;
}

float RadioReference::selfMean(RadioAspect a) const {
    if (_n < 2) return 0.0f;
    double sum = 0.0;
    int    n   = 0;
    for (int i = 0; i < _n; ++i) {
        for (int j = i + 1; j < _n; ++j) {
            bool        ok = false;
            const float v  = measure(a, _captures[i], _captures[j], &ok);
            if (!ok) continue;
            sum += static_cast<double>(v);
            ++n;
        }
    }
    return n ? static_cast<float>(sum / n) : 0.0f;
}

float RadioReference::selfSpread(RadioAspect a) const {
    if (_n < 3) return 0.0f;   // two captures make one pair, and one pair has no spread
    const float mean = selfMean(a);
    double      sum  = 0.0;
    int         n    = 0;
    for (int i = 0; i < _n; ++i) {
        for (int j = i + 1; j < _n; ++j) {
            bool        ok = false;
            const float v  = measure(a, _captures[i], _captures[j], &ok);
            if (!ok) continue;
            const double d = static_cast<double>(v) - static_cast<double>(mean);
            sum += d * d;
            ++n;
        }
    }
    return (n > 1) ? static_cast<float>(std::sqrt(sum / (n - 1))) : 0.0f;
}

float RadioReference::usedSpread(RadioAspect a) const {
    const float floorV = (a == RadioAspect::Set) ? kSetSpreadFloor
                                                 : kSignalSpreadFloorDb;
    const float s = selfSpread(a);
    return (s > floorV) ? s : floorV;
}

bool RadioReference::signalUsable(const RadioPrint& p) const {
    if (!ready() || !p.valid) return false;
    for (int i = 0; i < _n; ++i) {
        int n = 0;
        radioSignalDistance(p, _captures[i], &n);
        if (n >= kRadioMinShared) return true;
    }
    return false;
}

float RadioReference::z(RadioAspect a, const RadioPrint& p) const {
    if (!ready() || !p.valid) return 0.0f;

    double sum = 0.0;
    int    n   = 0;
    for (int i = 0; i < _n; ++i) {
        bool        ok = false;
        const float v  = measure(a, p, _captures[i], &ok);
        if (!ok) continue;
        sum += static_cast<double>(v);
        ++n;
    }
    if (n == 0) return 0.0f;

    const float mine = static_cast<float>(sum / n);
    const float self = selfMean(a);
    // SET is an agreement and SIGNAL a difference, so "worse than the place's
    // own visits" is a fall in one and a rise in the other. Both come out
    // positive, because everywhere else on this device a z is a distance.
    const float away = (a == RadioAspect::Set) ? (self - mine) : (mine - self);
    const float zz   = away / usedSpread(a);
    return (zz > 0.0f) ? zz : 0.0f;
}

bool RadioReference::meanFor(uint32_t id, float& dbm, int& seen) const {
    dbm  = 0.0f;
    seen = 0;
    if (id == 0) return false;

    double sum = 0.0;
    for (int i = 0; i < _n; ++i) {
        const RadioPrint& c = _captures[i];
        for (int k = 0; k < c.count; ++k) {
            if (c.id[k] != id) continue;
            sum += c.rssi[k];
            ++seen;
            break;
        }
    }
    if (seen == 0) return false;
    dbm = static_cast<float>(sum / seen);
    return true;
}

int RadioReference::missingFrom(const RadioPrint& p) const {
    if (_n == 0) return 0;
    // A majority, so one visit that caught a passing hotspot does not make
    // every later capture look like it lost something.
    const int need = (_n / 2) + 1;

    int missing = 0;
    for (int i = 0; i < _n; ++i) {
        const RadioPrint& c = _captures[i];
        for (int k = 0; k < c.count; ++k) {
            const uint32_t id = c.id[k];

            // Only from the first capture that carries it, or an access point
            // in all four visits would be counted four times.
            bool counted = false;
            for (int e = 0; e < i && !counted; ++e) {
                for (int m = 0; m < _captures[e].count; ++m) {
                    if (_captures[e].id[m] == id) { counted = true; break; }
                }
            }
            if (counted) continue;

            float here = 0.0f;
            int   seen = 0;
            meanFor(id, here, seen);
            if (seen < need) continue;

            bool present = false;
            for (int m = 0; m < p.count; ++m) {
                if (p.id[m] == id) { present = true; break; }
            }
            if (!present) ++missing;
        }
    }
    return missing;
}

RadioVerdict RadioReference::judge(const RadioPrint& p) const {
    if (!p.valid) return RadioVerdict::Unmeasured;
    if (_n == 0) return RadioVerdict::NoReference;
    if (!ready()) return RadioVerdict::TooFew;

    const float zs = z(RadioAspect::Set, p);
    if (zs > kDifferSigma) return RadioVerdict::Different;
    if (zs > kMatchSigma) return RadioVerdict::Unsure;

    // The set agrees. The pattern is a second, independent look at the same
    // question and can only take a match away: routers present at levels this
    // place has never shown are a reason to say nothing. With too few shared to
    // ask, the set carries it.
    if (!signalUsable(p)) return RadioVerdict::Match;
    return (z(RadioAspect::Signal, p) > kMatchSigma) ? RadioVerdict::Unsure
                                                     : RadioVerdict::Match;
}

// -----------------------------------------------------------------------------
int bestRadioPlace(const RadioReference* places, int n, const RadioPrint& p,
                   float* bestZ, float* margin) {
    if (bestZ) *bestZ = 0.0f;
    if (margin) *margin = 0.0f;
    if (!places || n <= 0 || !p.valid) return -1;

    int   win = -1, second = -1;
    float winZ = 0.0f, secondZ = 0.0f;

    for (int i = 0; i < n; ++i) {
        if (!places[i].ready()) continue;
        const float zz = places[i].z(RadioAspect::Set, p);
        if (win < 0 || zz < winZ) {
            second  = win;
            secondZ = winZ;
            win     = i;
            winZ    = zz;
        } else if (second < 0 || zz < secondZ) {
            second  = i;
            secondZ = zz;
        }
    }
    if (win < 0) return -1;

    if (bestZ) *bestZ = winZ;
    if (margin) *margin = (second < 0) ? kRadioLoneMargin : (secondZ - winZ);
    return win;
}

}  // namespace sd
