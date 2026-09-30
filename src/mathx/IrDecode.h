#pragma once
// -----------------------------------------------------------------------------
//  IrDecode - consumer IR protocols, from a demodulated pulse train.
//
//  The StickS3's receiver is a 38 kHz demodulator module, so the carrier is
//  already stripped before anything reaches the RMT peripheral. What arrives is
//  the envelope: alternating marks (carrier on) and spaces, in microseconds.
//  Everything below works on that and nothing else, which is why it is all
//  host-testable: a synthetic pulse train is indistinguishable from a real one.
//
//  Two outputs, deliberately different things:
//
//    bits   the frame as transmitted, first bit first. This lines up with the
//           waveform on screen and with the diff view, because "which bit
//           changed" only means anything in transmission order.
//
//    value  the conventional number every other decoder prints, with the
//           LSB-first byte assembly NEC and friends use already undone.
//           Without it a capture cannot be cross-checked against anything.
// -----------------------------------------------------------------------------
#include <cstddef>
#include <cstdint>

namespace sd {

// -----------------------------------------------------------------------------
//  A demodulated envelope. Even indices are marks, odd indices are spaces, so a
//  train always begins with a mark: the receiver is idle until a burst arrives.
// -----------------------------------------------------------------------------
struct PulseTrain {
    /// Long enough for an air-conditioner blast, not just a remote. Those
    /// rarely decode to anything named, but they still draw, still diff over
    /// their first bits, and still replay verbatim, which is what matters.
    static constexpr int kMax = 1024;

    uint16_t us[kMax]{};
    uint16_t count     = 0;
    bool     truncated = false;  ///< ran out of room; the tail is missing

    void clear() { count = 0; truncated = false; }

    bool push(uint32_t d) {
        if (count >= kMax) { truncated = true; return false; }
        us[count++] = (d > 0xFFFFu) ? 0xFFFFu : static_cast<uint16_t>(d);
        return true;
    }

    static constexpr bool isMark(int i) { return (i & 1) == 0; }

    uint32_t totalUs() const;

    /// Level at `us` from the first mark: 1 while a burst is on, 0 otherwise.
    /// Times before and after the frame read as idle, which is right for the
    /// Manchester codes: their leading and trailing half-bits are silence.
    int levelAt(int32_t us) const;
};

// -----------------------------------------------------------------------------
//  One recorded edge: how long a level lasted, and which level it was.
// -----------------------------------------------------------------------------
struct PulseEdge {
    uint32_t us    = 0;
    uint8_t  level = 0;
};

/// Flattens a recorded edge list into a train, deciding for itself which level
/// means "carrier present".
///
/// `get(i)` returns the i'th edge in order; a zero duration ends the recording.
/// `bufferFull` says the recording stopped for want of room rather than because
/// the line went quiet: the one case where the last edge is not silence.
///
/// Working the polarity out from the data rather than a configuration flag
/// means nothing upstream has to be right about how a receiver is wired, or
/// about a driver's input inverter surviving the next framework update.
///
/// The decision is taken at the END of the recording, the part that is always
/// well defined: a capture can be armed mid-burst, but it stops for one reason.
/// Three shapes, three answers:
///
///   closed by a zero-duration marker   the last run is the closing burst, so
///                                      that level is a mark
///   closing silence recorded as a run  the last run is a gap, so the other
///                                      level is a mark
///   the buffer filled                  no ending; fall back to the first edge,
///                                      which leaves idle for a burst
///
/// `minPulseUs` folds away any run shorter than itself into the run in
/// progress. This glitch filter has to live here: RMT's own is 8 bits of APB
/// ticks, topping out near 3.2 us, two orders of magnitude below the shortest
/// burst any remote sends. Useful against electrical glitches, useless against
/// a lamp.
template <typename Get>
void buildTrain(int n, bool bufferFull, uint16_t minPulseUs, Get get,
                PulseTrain& out) {
    out.clear();
    if (n <= 0) return;
    if (bufferFull) out.truncated = true;

    int last = -1;
    for (int i = 0; i < n; ++i) {
        if (get(i).us == 0) break;
        last = i;
    }
    if (last < 0) return;

    uint8_t markLevel;
    if (bufferFull)        markLevel = get(0).level;
    else if (last + 1 < n) markLevel = get(last).level;                       // marker
    else markLevel = static_cast<uint8_t>(get(last).level ? 0 : 1);           // silence

    bool wantMark = true;
    for (int i = 0; i <= last; ++i) {
        const PulseEdge e      = get(i);
        const bool      isMark = (e.level == markLevel);
        if (out.count == 0) {
            // A capture armed mid-burst leaves a fragment of gap at the front.
            // A train starts on a mark, and on a real one.
            if (!isMark || e.us < minPulseUs) continue;
            out.push(e.us);
            wantMark = false;
        } else if (isMark == wantMark && e.us >= minPulseUs) {
            out.push(e.us);
            wantMark = !wantMark;
        } else {
            // Either a spike too short to be real, or two runs at the same
            // level in a row. That should not happen, but if the hardware
            // splits one, folding it back beats shifting every later pulse.
            const uint32_t merged = out.us[out.count - 1] + e.us;
            out.us[out.count - 1] =
                (merged > 0xFFFFu) ? 0xFFFFu : static_cast<uint16_t>(merged);
        }
    }

    // Whatever silence closed the frame is not part of it.
    if (out.count > 0 && !PulseTrain::isMark(out.count - 1)) --out.count;
}

// -----------------------------------------------------------------------------
//  A bit vector in transmission order. 128 bits covers every remote protocol
//  worth naming; air-conditioner frames run longer and are reported truncated
//  rather than silently clipped.
// -----------------------------------------------------------------------------
struct BitField {
    static constexpr int kMaxBits = 128;

    uint8_t  data[kMaxBits / 8]{};
    uint16_t count     = 0;
    bool     truncated = false;

    void clear();
    bool push(bool v);
    bool get(int i) const;
    void set(int i, bool v);

    /// `len` bits starting at `from`, first bit taken as the most significant.
    uint64_t msb(int from, int len) const;
    /// Same span, first bit taken as the least significant: the order NEC,
    /// Samsung, Kaseikyo and Sony actually send.
    uint64_t lsb(int from, int len) const;
};

// -----------------------------------------------------------------------------
enum class IrProtocol : uint8_t {
    Unknown = 0,
    NecRepeat,      ///< the "still held" frame; not a new keypress
    Nec,
    NecExt,         ///< 16-bit address, so the address complement check is void
    Samsung,
    Sony,
    Rc5,
    Rc6,
    Kaseikyo,       ///< Panasonic and the rest of the 48-bit family
    Jvc,
    Lg,
    Rca,
    Denon,          ///< Sharp shares the timing closely enough to share the name
    PulseDistance,  ///< unrecognised, but the bit is in the space
    PulseWidth,     ///< unrecognised, but the bit is in the mark
};

const char* irProtocolName(IrProtocol p);

// -----------------------------------------------------------------------------
//  Measured timing, taken from every frame whether it decoded or not.
//
//  An unrecognised remote is only a dead end if its numbers cannot be read off
//  the screen. With them you can look the protocol up, or at least see that a
//  header of 3400/1700 is nowhere near the 9000/4500 the app hoped for. Each
//  pair is the mean of the short and the long cluster; a fixed-width element
//  comes back with both halves the same.
// -----------------------------------------------------------------------------
struct IrTiming {
    uint16_t headMark  = 0, headSpace = 0;  ///< 0 when the frame has no header
    uint16_t markLo    = 0, markHi    = 0;
    uint16_t spaceLo   = 0, spaceHi   = 0;
};

/// Fills `out` from the body of `t`, skipping a leading header if there is one.
void measureTiming(const PulseTrain& t, IrTiming& out);

// -----------------------------------------------------------------------------
struct IrDecoded {
    IrProtocol proto = IrProtocol::Unknown;
    BitField   bits;

    uint64_t value   = 0;  ///< the conventional value for this protocol
    uint32_t address = 0;
    uint32_t command = 0;

    /// Where the fields sit in transmission order, so the diff view can say
    /// "the command changed" instead of "bits 19 and 22 changed".
    uint8_t addrStart = 0, addrLen = 0;
    uint8_t cmdStart  = 0, cmdLen  = 0;

    /// Index of the protocol's toggle bit, or -1. RC5 and RC6 flip this on
    /// every fresh keypress and leave it alone while a key is held, which is
    /// the most confusing thing about capturing them.
    int8_t toggleIndex = -1;

    /// Complement or parity check, where the protocol has one. False means the
    /// frame decoded but does not check out: a misread, or not this protocol.
    bool checksOk = true;

    IrTiming timing;              ///< measured, not nominal

    uint16_t carrierHz  = 38000;  ///< nominal; the demodulator cannot measure it
    uint16_t pulses     = 0;

    bool toggle() const {
        return toggleIndex >= 0 && bits.get(toggleIndex);
    }
    const char* name() const { return irProtocolName(proto); }
};

/// Identify `t`. False only when nothing at all could be made of it; an
/// unrecognised but well-formed frame still comes back with bits, tagged
/// PulseDistance or PulseWidth, because unknown remotes are worth diffing.
bool decodeIr(const PulseTrain& t, IrDecoded& out);

// -----------------------------------------------------------------------------
//  Diff - the reason this app exists.
// -----------------------------------------------------------------------------
enum class IrDiffVerdict : uint8_t {
    Mismatch,   ///< different protocols or lengths; nothing to compare
    Identical,
    ToggleOnly, ///< only the protocol's own toggle bit moved
    Address,
    Command,
    Mixed,
};

struct IrDiff {
    IrDiffVerdict verdict  = IrDiffVerdict::Mismatch;
    BitField      mask;             ///< 1 wherever the two frames differ
    uint16_t      changed  = 0;
    int16_t       first    = -1;
    int16_t       last     = -1;
};

IrDiff diffIr(const IrDecoded& a, const IrDecoded& b);
const char* irDiffName(IrDiffVerdict v);

// -----------------------------------------------------------------------------
//  Re-sending a frame with a different command
// -----------------------------------------------------------------------------

/// Width of the command field this frame can be rebuilt with, or 0 if it cannot
/// be rebuilt. The Manchester codes are excluded: their bits are edges rather
/// than widths, so there is no single element to widen.
int irCommandBits(const IrDecoded& d);

// -----------------------------------------------------------------------------
//  Learning a frame, the way a learning remote does
//  Learning a frame, the way a learning remote does
//
//  A universal remote in learn mode does not decode before it accepts. It takes
//  the raw timing, asks for the button twice, checks the two captures agree
//  WITHIN TOLERANCE, averages them and replays that. Decoding is a bonus used
//  for naming and for generating repeats, never a gate.
//
//  Requiring a decode rejects every remote whose protocol nobody wrote down,
//  and requiring bit-for-bit agreement throws away frames that never decoded,
//  which is most of the interesting ones.
// -----------------------------------------------------------------------------

/// Is this plausibly a remote frame, whatever protocol it is?
///
/// Deliberately loose, because it is not the filter that matters: two captures
/// agreeing is. Interference does not repeat itself to within a quarter of a
/// pulse width, so this only has to throw out what could not be a frame.
bool irPlausible(const PulseTrain& t);

/// Do two captures describe the same frame?
///
/// Compared as timings rather than decoded bits: the same button pressed twice
/// never produces identical microseconds, since the receiver's gain control
/// varies what it reports run to run.
bool irSameFrame(const PulseTrain& a, const PulseTrain& b, float tol = 0.25f,
                 uint16_t floorUs = 120);

/// Element-wise mean of two matching captures, which halves the jitter on every
/// edge before it is ever transmitted.
void irAverage(const PulseTrain& a, const PulseTrain& b, PulseTrain& out);

// -----------------------------------------------------------------------------
//  Regenerating a frame for transmission
// -----------------------------------------------------------------------------

/// Rebuilds a frame from its decoded bits using the protocol's PUBLISHED timing
/// rather than the timing it happened to be captured with.
///
/// This is what makes a replay work on real equipment. An IR demodulator has a
/// gain control and a fixed threshold, so it holds its output asserted past the
/// end of a burst: every mark it reports is roughly 100 us too long and every
/// gap that much too short. Replaying that transmits the distortion, and the
/// receiving device adds the same bias again, so a frame inside tolerance when
/// the remote sent it is outside it after two receivers.
///
/// Regenerating from the bits sends what the original remote meant to send.
///
/// False for protocols with no published timing to appeal to, the generic pulse
/// readers, which fall back on irDeskew().
bool irSynthesise(const IrDecoded& d, PulseTrain& out);

/// Removes the receiver's bias from a captured train: `us` off every burst and
/// on to every gap. For frames no decoder could name this is the best available,
/// so a mark is never cut by more than a third of itself.
void irDeskew(PulseTrain& t, uint16_t us);

/// Copies `in` into `out` with the command replaced.
///
/// The rebuild works on the pulse train rather than a per-protocol encoder,
/// because for every protocol that qualifies a bit lives in the width of one
/// element: setting a bit swaps that element between the short and long widths
/// the frame already uses. Those widths are measured from the capture, so a
/// rebuilt frame keeps the timing of the remote that produced it.
///
/// A protocol carrying a complement or parity byte gets it recomputed, or the
/// receiver would reject every frame.
bool irWithCommand(const PulseTrain& in, const IrDecoded& d, uint32_t command,
                   PulseTrain& out);

}  // namespace sd
