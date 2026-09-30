#include "mathx/IrDecode.h"

namespace sd {
namespace {

// -----------------------------------------------------------------------------
//  Timing match. Both a percentage and an absolute floor, because a 25% window
//  on a 560 us bit mark is 140 us, comfortably inside a cheap remote's jitter,
//  while 25% on a 9000 us header is 2250 us, wide enough to swallow Samsung's
//  4500 us one. The floor keeps short pulses forgiving; the percentages below
//  keep the headers apart.
// -----------------------------------------------------------------------------
constexpr bool near(uint32_t v, uint32_t ref, float tol, uint32_t floorUs = 80) {
    const uint32_t slack = floorUs + static_cast<uint32_t>(ref * tol);
    return (v + slack >= ref) && (v <= ref + slack);
}

/// How many leading pulses are a header: 2, or none.
///
/// A header is a first burst noticeably longer than anything after it. The
/// threshold is 1.6x rather than 2x because Sony's 2400 us leader is exactly
/// twice its own longest data mark, and a rule landing on a real case will land
/// on the wrong side of it.
int headerSkip(const PulseTrain& t) {
    if (t.count < 4) return 0;
    uint32_t maxBody = 0;
    for (int i = 2; i < t.count; i += 2)
        if (t.us[i] > maxBody) maxBody = t.us[i];
    if (maxBody == 0) return 0;
    return (static_cast<uint32_t>(t.us[0]) * 5 > maxBody * 8) ? 2 : 0;
}

/// Reads `n` pulse-distance bits from `start`: a fixed mark, then a space whose
/// length is the bit. NEC, Samsung and Kaseikyo all work this way.
bool readDistance(const PulseTrain& t, int start, int n, uint32_t mark,
                  uint32_t zero, uint32_t one, BitField& out) {
    const int need = start + 2 * n;
    if (need > t.count) return false;
    // And no longer than its own length plus the closing mark. Without this a
    // 32-bit reader decodes the first 32 bits of a 42-bit frame and reports a
    // confident wrong answer.
    if (t.count > need + 2) return false;
    for (int i = 0; i < n; ++i) {
        if (!near(t.us[start + 2 * i], mark, 0.40f)) return false;
        const uint32_t s = t.us[start + 2 * i + 1];
        if      (near(s, one,  0.30f)) out.push(true);
        else if (near(s, zero, 0.40f)) out.push(false);
        else return false;
    }
    return true;
}

// -----------------------------------------------------------------------------
//  NEC and the 32-bit pulse-distance family
// -----------------------------------------------------------------------------
bool tryNecRepeat(const PulseTrain& t, IrDecoded& o) {
    // 9 ms burst, a short 2.25 ms gap, one closing mark. Nothing else.
    if (t.count < 3 || t.count > 4) return false;
    if (!near(t.us[0], 9000, 0.22f)) return false;
    if (!near(t.us[1], 2250, 0.25f)) return false;
    if (!near(t.us[2],  560, 0.45f)) return false;
    o.proto     = IrProtocol::NecRepeat;
    o.carrierHz = 38000;
    return true;
}

bool tryNec(const PulseTrain& t, IrDecoded& o) {
    if (t.count < 4) return false;
    if (!near(t.us[0], 9000, 0.22f) || !near(t.us[1], 4500, 0.22f)) return false;

    BitField b;
    if (!readDistance(t, 2, 32, 560, 560, 1690, b)) return false;

    const uint8_t a0 = static_cast<uint8_t>(b.lsb(0, 8));
    const uint8_t a1 = static_cast<uint8_t>(b.lsb(8, 8));
    const uint8_t c0 = static_cast<uint8_t>(b.lsb(16, 8));
    const uint8_t c1 = static_cast<uint8_t>(b.lsb(24, 8));

    // A valid address complement separates plain NEC from the extended form,
    // where the second byte is the high half of a 16-bit address.
    const bool addrOk = (a0 == static_cast<uint8_t>(~a1));

    o.bits      = b;
    o.proto     = addrOk ? IrProtocol::Nec : IrProtocol::NecExt;
    o.address   = addrOk ? a0 : static_cast<uint32_t>(a0 | (a1 << 8));
    o.command   = c0;
    o.checksOk  = (c0 == static_cast<uint8_t>(~c1));
    o.value     = (static_cast<uint64_t>(a0) << 24) | (static_cast<uint64_t>(a1) << 16) |
                  (static_cast<uint64_t>(c0) << 8)  |  static_cast<uint64_t>(c1);
    o.addrStart = 0;  o.addrLen = 16;
    o.cmdStart  = 16; o.cmdLen  = 16;
    o.carrierHz = 38000;
    return true;
}

bool trySamsung(const PulseTrain& t, IrDecoded& o) {
    if (t.count < 4) return false;
    if (!near(t.us[0], 4500, 0.22f) || !near(t.us[1], 4500, 0.22f)) return false;

    BitField b;
    if (!readDistance(t, 2, 32, 560, 560, 1690, b)) return false;

    const uint8_t a0 = static_cast<uint8_t>(b.lsb(0, 8));
    const uint8_t a1 = static_cast<uint8_t>(b.lsb(8, 8));
    const uint8_t c0 = static_cast<uint8_t>(b.lsb(16, 8));
    const uint8_t c1 = static_cast<uint8_t>(b.lsb(24, 8));

    o.bits      = b;
    o.proto     = IrProtocol::Samsung;
    o.address   = a0;
    o.command   = c0;
    o.checksOk  = (c0 == static_cast<uint8_t>(~c1));
    o.value     = (static_cast<uint64_t>(a0) << 24) | (static_cast<uint64_t>(a1) << 16) |
                  (static_cast<uint64_t>(c0) << 8)  |  static_cast<uint64_t>(c1);
    o.addrStart = 0;  o.addrLen = 16;
    o.cmdStart  = 16; o.cmdLen  = 16;
    o.carrierHz = 38000;
    return true;
}

bool tryKaseikyo(const PulseTrain& t, IrDecoded& o) {
    if (t.count < 4) return false;
    if (!near(t.us[0], 3456, 0.22f) || !near(t.us[1], 1728, 0.22f)) return false;

    BitField b;
    if (!readDistance(t, 2, 48, 432, 432, 1296, b)) return false;

    uint8_t by[6];
    for (int i = 0; i < 6; ++i) by[i] = static_cast<uint8_t>(b.lsb(i * 8, 8));

    o.bits      = b;
    o.proto     = IrProtocol::Kaseikyo;
    o.address   = static_cast<uint32_t>(by[0] | (by[1] << 8));  // vendor code
    o.command   = by[4];
    // The trailing byte is the XOR of the three before it.
    o.checksOk  = (by[5] == (by[2] ^ by[3] ^ by[4]));
    uint64_t v = 0;
    for (int i = 0; i < 6; ++i) v = (v << 8) | by[i];
    o.value     = v;
    o.addrStart = 0;  o.addrLen = 16;
    o.cmdStart  = 16; o.cmdLen  = 32;
    o.carrierHz = 37000;
    return true;
}

// -----------------------------------------------------------------------------
//  The rest of the pulse-distance family. Every one is the same shape, a header
//  then bits in the gap widths, so they differ only in their numbers.
// -----------------------------------------------------------------------------
struct DistanceSpec {
    uint32_t headMark, headSpace, mark, zero, one;
    int      bits;
};

bool readFramed(const PulseTrain& t, const DistanceSpec& s, BitField& out) {
    if (t.count < 4) return false;
    if (!near(t.us[0], s.headMark, 0.22f)) return false;
    if (!near(t.us[1], s.headSpace, 0.22f)) return false;
    return readDistance(t, 2, s.bits, s.mark, s.zero, s.one, out);
}

bool tryJvc(const PulseTrain& t, IrDecoded& o) {
    BitField b;
    if (!readFramed(t, {8400, 4200, 526, 526, 1574, 16}, b)) return false;
    o.bits      = b;
    o.proto     = IrProtocol::Jvc;
    o.address   = static_cast<uint32_t>(b.lsb(0, 8));
    o.command   = static_cast<uint32_t>(b.lsb(8, 8));
    o.value     = b.lsb(0, 16);
    o.addrStart = 0; o.addrLen = 8;
    o.cmdStart  = 8; o.cmdLen  = 8;
    o.carrierHz = 38000;
    return true;
}

bool tryLg(const PulseTrain& t, IrDecoded& o) {
    BitField b;
    if (!readFramed(t, {8500, 4250, 500, 550, 1600, 28}, b)) return false;
    // LG sends most significant bit first and closes with a nibble that is the
    // sum of the command's four nibbles.
    const uint32_t cmd = static_cast<uint32_t>(b.msb(8, 16));
    const uint32_t sum = ((cmd >> 12) + (cmd >> 8) + (cmd >> 4) + cmd) & 0x0Fu;

    o.bits      = b;
    o.proto     = IrProtocol::Lg;
    o.address   = static_cast<uint32_t>(b.msb(0, 8));
    o.command   = cmd;
    o.checksOk  = (static_cast<uint32_t>(b.msb(24, 4)) == sum);
    o.value     = b.msb(0, 28);
    o.addrStart = 0; o.addrLen = 8;
    o.cmdStart  = 8; o.cmdLen  = 16;
    o.carrierHz = 38000;
    return true;
}

bool tryRca(const PulseTrain& t, IrDecoded& o) {
    BitField b;
    if (!readFramed(t, {4000, 4000, 500, 1000, 2000, 24}, b)) return false;
    const uint32_t addr = static_cast<uint32_t>(b.msb(0, 4));
    const uint32_t cmd  = static_cast<uint32_t>(b.msb(4, 8));

    o.bits      = b;
    o.proto     = IrProtocol::Rca;
    o.address   = addr;
    o.command   = cmd;
    o.checksOk  = (static_cast<uint32_t>(b.msb(12, 4)) == (~addr & 0x0Fu)) &&
                  (static_cast<uint32_t>(b.msb(16, 8)) == (~cmd & 0xFFu));
    o.value     = b.msb(0, 24);
    o.addrStart = 0; o.addrLen = 4;
    o.cmdStart  = 4; o.cmdLen  = 8;
    o.carrierHz = 56000;  // RCA runs high; the 38 kHz front end will be down a few dB
    return true;
}

bool tryDenon(const PulseTrain& t, IrDecoded& o) {
    // No header at all, which is what tells it apart from everything above.
    BitField b;
    if (!readDistance(t, 0, 15, 264, 792, 1848, b)) return false;
    o.bits      = b;
    o.proto     = IrProtocol::Denon;
    o.address   = static_cast<uint32_t>(b.msb(0, 5));
    o.command   = static_cast<uint32_t>(b.msb(5, 8));
    o.value     = b.msb(0, 15);
    o.addrStart = 0; o.addrLen = 5;
    o.cmdStart  = 5; o.cmdLen  = 8;
    o.carrierHz = 38000;
    return true;
}

// -----------------------------------------------------------------------------
//  Sony SIRC - the bit lives in the mark, not the space
// -----------------------------------------------------------------------------
bool trySony(const PulseTrain& t, IrDecoded& o) {
    // 12 bits is the shortest SIRC frame: header pair plus 24 body pulses, one
    // of which is the closing space that never gets captured.
    if (t.count < 25) return false;
    // Sony's leader burst is close enough to RC6's that the burst alone cannot
    // separate them; the gap does, 600 us against 889 us. The two windows below
    // are narrow enough not to overlap.
    if (!near(t.us[0], 2400, 0.25f)) return false;
    if (!near(t.us[1],  600, 0.22f, 40)) return false;

    // The closing space is silence and is often never captured, so a frame can
    // be one pulse short of its nominal length.
    const int avail = (t.count - 1) / 2;
    const int n = (avail >= 20) ? 20 : (avail >= 15) ? 15 : (avail >= 12) ? 12 : 0;
    if (n == 0) return false;

    BitField b;
    for (int i = 0; i < n; ++i) {
        const uint32_t m = t.us[2 + 2 * i];
        if      (near(m, 1200, 0.30f)) b.push(true);
        else if (near(m,  600, 0.35f)) b.push(false);
        else return false;
        const int si = 3 + 2 * i;
        if (si < t.count && !near(t.us[si], 600, 0.45f)) return false;
    }

    o.bits      = b;
    o.proto     = IrProtocol::Sony;
    o.command   = static_cast<uint32_t>(b.lsb(0, 7));
    o.address   = static_cast<uint32_t>(b.lsb(7, n - 7));
    o.value     = b.lsb(0, n);
    o.cmdStart  = 0; o.cmdLen  = 7;
    o.addrStart = 7; o.addrLen = static_cast<uint8_t>(n - 7);
    o.carrierHz = 40000;
    return true;
}

// -----------------------------------------------------------------------------
//  The Manchester pair. Both are sampled rather than edge-matched: build the
//  bit grid from the nominal timing, read the level in the middle of each half
//  bit, and let levelAt() report silence outside the capture. That is the only
//  way to recover RC5's leading half bit, which is a space and so was never
//  transmitted as anything the receiver could see.
// -----------------------------------------------------------------------------
constexpr int32_t kRc5Half = 889;   // half a 1778 us bit
constexpr int32_t kRc6Unit = 444;

bool tryRc5(const PulseTrain& t, IrDecoded& o) {
    if (t.count < 8) return false;
    // The first mark is one or two half bits: bit 0 is always 1, so its second
    // half is a burst, and bit 1 extends it only when bit 1 is 0.
    if (!near(t.us[0], 889, 0.35f) && !near(t.us[0], 1778, 0.30f)) return false;

    // 14 bits of 1778 us, less the invisible leading half bit.
    const uint32_t total = t.totalUs();
    if (total < 21500 || total > 25500) return false;

    BitField b;
    for (int k = 0; k < 14; ++k) {
        const int32_t base = -kRc5Half + k * 2 * kRc5Half;
        const int lo = t.levelAt(base + kRc5Half / 2);
        const int hi = t.levelAt(base + kRc5Half + kRc5Half / 2);
        if      (lo == 0 && hi == 1) b.push(true);    // RC5 one is space->mark
        else if (lo == 1 && hi == 0) b.push(false);
        else return false;
    }
    if (!b.get(0)) return false;  // S1 is always 1

    // S2 low is the extended form: it carries the seventh command bit inverted.
    const uint32_t cmd = static_cast<uint32_t>(b.msb(8, 6)) + (b.get(1) ? 0u : 64u);

    o.bits        = b;
    o.proto       = IrProtocol::Rc5;
    o.toggleIndex = 2;
    o.address     = static_cast<uint32_t>(b.msb(3, 5));
    o.command     = cmd;
    o.value       = b.msb(0, 14);
    o.addrStart   = 3; o.addrLen = 5;
    o.cmdStart    = 8; o.cmdLen  = 6;
    o.carrierHz   = 36000;
    return true;
}

bool tryRc6(const PulseTrain& t, IrDecoded& o) {
    if (t.count < 12) return false;
    if (!near(t.us[0], 2666, 0.20f)) return false;      // leader burst, 6 units
    if (!near(t.us[1],  889, 0.22f, 40)) return false;  // leader gap, 2 units

    BitField b;
    int32_t cur = 2666 + 889;
    for (int k = 0; k < 21; ++k) {
        // The trailer bit, the toggle, is sent at double width so a receiver
        // can find it without counting.
        const int32_t half = (k == 4) ? 2 * kRc6Unit : kRc6Unit;
        const int lo = t.levelAt(cur + half / 2);
        const int hi = t.levelAt(cur + half + half / 2);
        if      (lo == 1 && hi == 0) b.push(true);   // RC6 one is mark->space
        else if (lo == 0 && hi == 1) b.push(false);
        else return false;
        cur += 2 * half;
    }
    if (!b.get(0)) return false;                       // start bit
    if (b.get(1) || b.get(2) || b.get(3)) return false;  // mode 0 only

    o.bits        = b;
    o.proto       = IrProtocol::Rc6;
    o.toggleIndex = 4;
    o.address     = static_cast<uint32_t>(b.msb(5, 8));
    o.command     = static_cast<uint32_t>(b.msb(13, 8));
    o.value       = b.msb(0, 21);
    o.addrStart   = 5;  o.addrLen = 8;
    o.cmdStart    = 13; o.cmdLen  = 8;
    o.carrierHz   = 36000;
    return true;
}

// -----------------------------------------------------------------------------
//  Last resort. An unrecognised remote is the interesting case, so as long as
//  the frame has two clearly separated pulse lengths it gets decoded anyway and
//  can be diffed like any other.
// -----------------------------------------------------------------------------
bool twoLevels(uint32_t lo, uint32_t hi) {
    return hi > lo * 17 / 10 + 100;
}

bool tryGeneric(const PulseTrain& t, IrDecoded& o) {
    if (t.count < 18) return false;

    const int start = headerSkip(t);

    const int nDist  = (t.count - start) / 2;
    const int nWidth = (t.count - start + 1) / 2;
    if (nDist < 8) return false;

    uint32_t minS = 0xFFFFFFFFu, maxS = 0, minM = 0xFFFFFFFFu, maxM = 0;
    for (int i = 0; i < nDist; ++i) {
        const uint32_t s = t.us[start + 2 * i + 1];
        if (s < minS) minS = s;
        if (s > maxS) maxS = s;
    }
    for (int i = 0; i < nWidth; ++i) {
        const uint32_t m = t.us[start + 2 * i];
        if (m < minM) minM = m;
        if (m > maxM) maxM = m;
    }

    int      n     = 0;
    uint32_t mid   = 0;
    int      first = 0, step = 2;
    if (twoLevels(minS, maxS)) {
        o.proto = IrProtocol::PulseDistance;
        n = nDist;  mid = (minS + maxS) / 2;  first = start + 1;
    } else if (twoLevels(minM, maxM)) {
        o.proto = IrProtocol::PulseWidth;
        n = nWidth; mid = (minM + maxM) / 2;  first = start;
    } else {
        return false;
    }

    BitField b;
    if (n > BitField::kMaxBits) { n = BitField::kMaxBits; b.truncated = true; }
    for (int i = 0; i < n; ++i) b.push(t.us[first + i * step] > mid);

    o.bits      = b;
    o.value     = b.msb(0, (n > 64) ? 64 : n);
    o.carrierHz = 38000;
    return true;
}

}  // namespace

// -----------------------------------------------------------------------------
uint32_t PulseTrain::totalUs() const {
    uint32_t sum = 0;
    for (int i = 0; i < count; ++i) sum += us[i];
    return sum;
}

int PulseTrain::levelAt(int32_t at) const {
    if (at < 0) return 0;
    int32_t acc = 0;
    for (int i = 0; i < count; ++i) {
        acc += us[i];
        if (at < acc) return isMark(i) ? 1 : 0;
    }
    return 0;
}

// -----------------------------------------------------------------------------
void BitField::clear() {
    for (auto& byte : data) byte = 0;
    count     = 0;
    truncated = false;
}

bool BitField::push(bool v) {
    if (count >= kMaxBits) { truncated = true; return false; }
    set(count++, v);
    return true;
}

bool BitField::get(int i) const {
    if (i < 0 || i >= kMaxBits) return false;
    return (data[i >> 3] >> (i & 7)) & 1u;
}

void BitField::set(int i, bool v) {
    if (i < 0 || i >= kMaxBits) return;
    const uint8_t m = static_cast<uint8_t>(1u << (i & 7));
    if (v) data[i >> 3] |= m;
    else   data[i >> 3] = static_cast<uint8_t>(data[i >> 3] & ~m);
}

uint64_t BitField::msb(int from, int len) const {
    uint64_t v = 0;
    for (int i = 0; i < len; ++i) v = (v << 1) | (get(from + i) ? 1u : 0u);
    return v;
}

uint64_t BitField::lsb(int from, int len) const {
    uint64_t v = 0;
    for (int i = 0; i < len; ++i)
        if (get(from + i)) v |= (1ull << i);
    return v;
}

// -----------------------------------------------------------------------------
namespace {

/// Mean of the values below and above the midpoint, walked with a stride so no
/// temporary array is needed. A fixed-width element puts everything in the low
/// bucket and returns the same number twice, which is the honest answer.
void clusterMeans(const PulseTrain& t, int first, int count, uint16_t& lo,
                  uint16_t& hi) {
    lo = hi = 0;
    if (count <= 0) return;

    uint32_t mn = 0xFFFFFFFFu, mx = 0;
    for (int i = 0; i < count; ++i) {
        const uint32_t v = t.us[first + 2 * i];
        if (v < mn) mn = v;
        if (v > mx) mx = v;
    }
    const uint32_t mid = (mn + mx) / 2;

    uint32_t sumLo = 0, nLo = 0, sumHi = 0, nHi = 0;
    for (int i = 0; i < count; ++i) {
        const uint32_t v = t.us[first + 2 * i];
        if (v <= mid) { sumLo += v; ++nLo; }
        else          { sumHi += v; ++nHi; }
    }
    lo = nLo ? static_cast<uint16_t>(sumLo / nLo) : 0;
    hi = nHi ? static_cast<uint16_t>(sumHi / nHi) : lo;
}

}  // namespace

void measureTiming(const PulseTrain& t, IrTiming& out) {
    out = IrTiming{};
    if (t.count < 4) return;

    const int start = headerSkip(t);
    if (start) {
        out.headMark  = t.us[0];
        out.headSpace = t.us[1];
    }

    const int marks  = (t.count - start + 1) / 2;
    const int spaces = (t.count - start) / 2;
    clusterMeans(t, start,     marks,  out.markLo,  out.markHi);
    clusterMeans(t, start + 1, spaces, out.spaceLo, out.spaceHi);
}

// -----------------------------------------------------------------------------
const char* irProtocolName(IrProtocol p) {
    switch (p) {
        case IrProtocol::NecRepeat:     return "NEC RPT";
        case IrProtocol::Nec:           return "NEC";
        case IrProtocol::NecExt:        return "NEC EXT";
        case IrProtocol::Samsung:       return "SAMSUNG";
        case IrProtocol::Sony:          return "SONY";
        case IrProtocol::Rc5:           return "RC5";
        case IrProtocol::Rc6:           return "RC6";
        case IrProtocol::Kaseikyo:      return "KASEIKYO";
        case IrProtocol::Jvc:           return "JVC";
        case IrProtocol::Lg:            return "LG";
        case IrProtocol::Rca:           return "RCA";
        case IrProtocol::Denon:         return "DENON";
        case IrProtocol::PulseDistance: return "PULSE-D";
        case IrProtocol::PulseWidth:    return "PULSE-W";
        default:                        return "UNKNOWN";
    }
}

const char* irDiffName(IrDiffVerdict v) {
    switch (v) {
        case IrDiffVerdict::Identical:  return "IDENTICAL";
        case IrDiffVerdict::ToggleOnly: return "TOGGLE";
        case IrDiffVerdict::Address:    return "ADDR";
        case IrDiffVerdict::Command:    return "CMD";
        case IrDiffVerdict::Mixed:      return "MIXED";
        default:                        return "NO MATCH";
    }
}

// -----------------------------------------------------------------------------
bool decodeIr(const PulseTrain& t, IrDecoded& out) {
    out            = IrDecoded{};
    out.pulses     = t.count;
    measureTiming(t, out.timing);
    if (t.count < 3) return false;

    // Order matters. The repeat frame is a prefix of nothing else and is first;
    // RC6 before RC5 because RC6 has a leader RC5 lacks, and both before Sony
    // because their leaders are more distinctive; the generic reader last,
    // since it will decode anything.
    if (tryNecRepeat(t, out)) return true;
    if (tryNec(t, out))       return true;
    if (trySamsung(t, out))   return true;
    if (tryKaseikyo(t, out))  return true;
    if (tryJvc(t, out))       return true;
    if (tryLg(t, out))        return true;
    if (tryRca(t, out))       return true;
    if (tryRc6(t, out))       return true;
    if (tryRc5(t, out))       return true;
    if (trySony(t, out))      return true;
    if (tryDenon(t, out))     return true;
    if (tryGeneric(t, out))   return true;

    out.proto = IrProtocol::Unknown;
    return false;
}

// -----------------------------------------------------------------------------
namespace {

/// Which element of a bit pair carries the bit.
enum class BitSite : uint8_t { None, Space, Mark };

BitSite bitSite(IrProtocol p) {
    switch (p) {
        case IrProtocol::Nec:
        case IrProtocol::NecExt:
        case IrProtocol::Samsung:
        case IrProtocol::Kaseikyo:
        case IrProtocol::Jvc:
        case IrProtocol::Lg:
        case IrProtocol::Rca:
        case IrProtocol::Denon:
        case IrProtocol::PulseDistance: return BitSite::Space;
        case IrProtocol::Sony:
        case IrProtocol::PulseWidth:    return BitSite::Mark;
        default:                        return BitSite::None;
    }
}

/// Where the key code sits, how wide it is, and which end goes first. One table
/// because the alternative is the same switch in three places, drifting apart.
struct CmdField {
    int  start    = 0;
    int  width    = 0;
    bool lsbFirst = true;
};

bool cmdField(const IrDecoded& d, CmdField& f) {
    switch (d.proto) {
        case IrProtocol::Nec:
        case IrProtocol::NecExt:
        case IrProtocol::Samsung:  f = {d.cmdStart, 8, true}; break;
        case IrProtocol::Kaseikyo: f = {32, 8, true};         break;  // the key byte
        case IrProtocol::Sony:     f = {0, 7, true};          break;
        case IrProtocol::Jvc:      f = {8, 8, true};          break;
        case IrProtocol::Lg:       f = {8, 16, false};        break;
        case IrProtocol::Rca:      f = {4, 8, false};         break;
        case IrProtocol::Denon:    f = {5, 8, false};         break;
        case IrProtocol::PulseDistance:
        case IrProtocol::PulseWidth: f = {d.bits.count - 8, 8, false}; break;
        default: return false;
    }
    return f.width > 0 && f.start >= 0 && f.start + f.width <= d.bits.count;
}

/// Writes `value` into `b` at `f`, then repairs whatever check field the
/// protocol carries. A stale checksum is a frame a real receiver throws away,
/// which would make stepping through commands look like a broken transmitter.
void writeCommand(BitField& b, const IrDecoded& d, const CmdField& f,
                  uint32_t value) {
    for (int i = 0; i < f.width; ++i) {
        const int shift = f.lsbFirst ? i : (f.width - 1 - i);
        b.set(f.start + i, (value >> shift) & 1u);
    }

    switch (d.proto) {
        case IrProtocol::Nec:
        case IrProtocol::NecExt:
        case IrProtocol::Samsung: {
            if (f.start + 16 > d.bits.count) break;
            const uint8_t inv = static_cast<uint8_t>(~b.lsb(f.start, 8));
            for (int i = 0; i < 8; ++i) b.set(f.start + 8 + i, (inv >> i) & 1u);
            break;
        }
        case IrProtocol::Kaseikyo: {
            const uint8_t par = static_cast<uint8_t>(b.lsb(16, 8)) ^
                                static_cast<uint8_t>(b.lsb(24, 8)) ^
                                static_cast<uint8_t>(b.lsb(32, 8));
            for (int i = 0; i < 8; ++i) b.set(40 + i, (par >> i) & 1u);
            break;
        }
        case IrProtocol::Lg: {
            const uint32_t sum =
                ((value >> 12) + (value >> 8) + (value >> 4) + value) & 0x0Fu;
            for (int i = 0; i < 4; ++i) b.set(24 + i, (sum >> (3 - i)) & 1u);
            break;
        }
        case IrProtocol::Rca: {
            const uint32_t inv = ~value & 0xFFu;
            for (int i = 0; i < 8; ++i) b.set(16 + i, (inv >> (7 - i)) & 1u);
            break;
        }
        default: break;
    }
}

}  // namespace

int irCommandBits(const IrDecoded& d) {
    const BitSite site = bitSite(d.proto);
    if (site == BitSite::None || d.bits.count == 0) return 0;

    // Rebuilding needs two clearly separated widths to swap between. A frame
    // whose bits all came out the same collapses the clusters, and there is
    // then no honest way to widen anything.
    const uint32_t lo = (site == BitSite::Space) ? d.timing.spaceLo : d.timing.markLo;
    const uint32_t hi = (site == BitSite::Space) ? d.timing.spaceHi : d.timing.markHi;
    if (lo == 0 || hi < lo * 3 / 2) return 0;

    CmdField f;
    return cmdField(d, f) ? f.width : 0;
}

bool irWithCommand(const PulseTrain& in, const IrDecoded& d, uint32_t command,
                   PulseTrain& out) {
    if (irCommandBits(d) == 0) return false;
    CmdField f;
    if (!cmdField(d, f)) return false;

    BitField b = d.bits;
    writeCommand(b, d, f, command);

    out = in;
    const BitSite  site = bitSite(d.proto);
    const int      hdr  = d.timing.headMark ? 2 : 0;
    const int      off  = (site == BitSite::Space) ? 1 : 0;
    const uint16_t lo   = (site == BitSite::Space) ? d.timing.spaceLo : d.timing.markLo;
    const uint16_t hi   = (site == BitSite::Space) ? d.timing.spaceHi : d.timing.markHi;

    for (int i = 0; i < d.bits.count; ++i) {
        const int idx = hdr + 2 * i + off;
        if (idx >= out.count) return false;
        out.us[idx] = b.get(i) ? hi : lo;
    }
    return true;
}


// -----------------------------------------------------------------------------
namespace {

/// Header, then one fixed burst and a gap whose width is the bit.
bool emitDistance(PulseTrain& t, const BitField& b, uint32_t hm, uint32_t hs,
                  uint32_t mark, uint32_t zero, uint32_t one) {
    if (b.count == 0) return false;
    if (hm) { t.push(hm); t.push(hs); }
    for (int i = 0; i < b.count; ++i) {
        t.push(mark);
        t.push(b.get(i) ? one : zero);
    }
    t.push(mark);  // closing burst
    return true;
}

/// Header, then a burst whose width is the bit, separated by fixed gaps.
bool emitWidth(PulseTrain& t, const BitField& b, uint32_t hm, uint32_t hs,
               uint32_t zero, uint32_t one, uint32_t gap) {
    if (b.count == 0) return false;
    t.push(hm);
    t.push(hs);
    for (int i = 0; i < b.count; ++i) {
        t.push(b.get(i) ? one : zero);
        if (i + 1 < b.count) t.push(gap);  // the closing gap is silence
    }
    return true;
}

/// Merges a half-bit level sequence into runs, dropping the silence at each end
/// exactly as a transmitter would.
void emitHalves(PulseTrain& t, const uint8_t* level, const uint16_t* width, int n) {
    int i = 0;
    while (i < n && level[i] == 0) ++i;  // a leading gap is never transmitted
    while (i < n) {
        const uint8_t lvl = level[i];
        uint32_t      w   = 0;
        while (i < n && level[i] == lvl) { w += width[i]; ++i; }
        t.push(w);
    }
    if (t.count > 0 && !PulseTrain::isMark(t.count - 1)) --t.count;
}

bool emitRc5(PulseTrain& t, const BitField& b) {
    if (b.count != 14) return false;
    uint8_t  lvl[28];
    uint16_t w[28];
    for (int k = 0; k < 14; ++k) {
        const bool v = b.get(k);
        lvl[2 * k]     = v ? 0 : 1;  // RC5 one is gap then burst
        lvl[2 * k + 1] = v ? 1 : 0;
        w[2 * k] = w[2 * k + 1] = 889;
    }
    emitHalves(t, lvl, w, 28);
    return true;
}

bool emitRc6(PulseTrain& t, const BitField& b) {
    if (b.count != 21) return false;
    uint8_t  lvl[42];
    uint16_t w[42];
    for (int k = 0; k < 21; ++k) {
        const bool     v  = b.get(k);
        const uint16_t hw = (k == 4) ? 888 : 444;  // the trailer runs double width
        lvl[2 * k]     = v ? 1 : 0;                // RC6 one is burst then gap
        lvl[2 * k + 1] = v ? 0 : 1;
        w[2 * k] = w[2 * k + 1] = hw;
    }
    t.push(2666);
    t.push(889);
    emitHalves(t, lvl, w, 42);
    return true;
}

}  // namespace

bool irSynthesise(const IrDecoded& d, PulseTrain& out) {
    out.clear();
    const BitField& b = d.bits;
    switch (d.proto) {
        case IrProtocol::NecRepeat:
            out.push(9000); out.push(2250); out.push(560);
            return true;
        case IrProtocol::Nec:
        case IrProtocol::NecExt:  return emitDistance(out, b, 9000, 4500, 560, 560, 1690);
        case IrProtocol::Samsung: return emitDistance(out, b, 4500, 4500, 560, 560, 1690);
        case IrProtocol::Kaseikyo:return emitDistance(out, b, 3456, 1728, 432, 432, 1296);
        case IrProtocol::Jvc:     return emitDistance(out, b, 8400, 4200, 526, 526, 1574);
        case IrProtocol::Lg:      return emitDistance(out, b, 8500, 4250, 500, 550, 1600);
        case IrProtocol::Rca:     return emitDistance(out, b, 4000, 4000, 500, 1000, 2000);
        case IrProtocol::Denon:   return emitDistance(out, b, 0, 0, 264, 792, 1848);
        case IrProtocol::Sony:    return emitWidth(out, b, 2400, 600, 600, 1200, 600);
        case IrProtocol::Rc5:     return emitRc5(out, b);
        case IrProtocol::Rc6:     return emitRc6(out, b);
        default:                  return false;  // no published timing to appeal to
    }
}

void irDeskew(PulseTrain& t, uint16_t us) {
    for (int i = 0; i < t.count; ++i) {
        const uint32_t v = t.us[i];
        if (PulseTrain::isMark(i)) {
            // Never take more than a third: on a protocol with 264 us bursts a
            // flat 100 us correction is a quarter of the pulse, and being wrong
            // about the bias must not be worse than ignoring it.
            const uint32_t cut = (us < v / 3) ? us : v / 3;
            t.us[i] = static_cast<uint16_t>(v - cut);
        } else {
            const uint32_t add = v + us;
            t.us[i] = (add > 0xFFFFu) ? 0xFFFFu : static_cast<uint16_t>(add);
        }
    }
}

// -----------------------------------------------------------------------------
bool irPlausible(const PulseTrain& t) {
    // Four bits is the shortest thing anyone calls a code.
    if (t.count < 8) return false;

    const uint32_t total = t.totalUs();
    if (total < 4000u || total > 250000u) return false;

    for (int i = 0; i < t.count; ++i) {
        // Under 100 us is a spike no protocol sends; over 30 ms is a gap that
        // should have ended the capture rather than appeared inside it.
        if (t.us[i] < 100u || t.us[i] > 30000u) return false;
    }
    return true;
}

bool irSameFrame(const PulseTrain& a, const PulseTrain& b, float tol,
                 uint16_t floorUs) {
    if (a.count != b.count || a.count == 0) return false;
    for (int i = 0; i < a.count; ++i) {
        const uint32_t x = a.us[i];
        const uint32_t y = b.us[i];
        const uint32_t slack =
            floorUs + static_cast<uint32_t>((x < y ? x : y) * tol);
        const uint32_t diff = (x > y) ? (x - y) : (y - x);
        if (diff > slack) return false;
    }
    return true;
}

void irAverage(const PulseTrain& a, const PulseTrain& b, PulseTrain& out) {
    out.clear();
    const int n = (a.count < b.count) ? a.count : b.count;
    for (int i = 0; i < n; ++i)
        out.push((static_cast<uint32_t>(a.us[i]) + b.us[i] + 1) / 2);
    out.truncated = a.truncated || b.truncated;
}

// -----------------------------------------------------------------------------
IrDiff diffIr(const IrDecoded& a, const IrDecoded& b) {
    IrDiff d;
    if (a.proto != b.proto || a.bits.count != b.bits.count || a.bits.count == 0)
        return d;  // Mismatch

    for (int i = 0; i < a.bits.count; ++i) {
        const bool differs = a.bits.get(i) != b.bits.get(i);
        d.mask.push(differs);
        if (differs) {
            if (d.first < 0) d.first = static_cast<int16_t>(i);
            d.last = static_cast<int16_t>(i);
            ++d.changed;
        }
    }

    if (d.changed == 0) {
        d.verdict = IrDiffVerdict::Identical;
    } else if (a.toggleIndex >= 0 && d.changed == 1 && d.first == a.toggleIndex) {
        d.verdict = IrDiffVerdict::ToggleOnly;
    } else {
        const bool inAddr = a.addrLen > 0 && d.first >= a.addrStart &&
                            d.last < a.addrStart + a.addrLen;
        const bool inCmd  = a.cmdLen > 0 && d.first >= a.cmdStart &&
                            d.last < a.cmdStart + a.cmdLen;
        d.verdict = inCmd ? IrDiffVerdict::Command
                  : inAddr ? IrDiffVerdict::Address
                           : IrDiffVerdict::Mixed;
    }
    return d;
}

}  // namespace sd
