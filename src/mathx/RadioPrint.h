#pragma once
// -----------------------------------------------------------------------------
//  RadioPrint - telling one room from another by the access points it can see.
//
//  The set of access points a room hears is very nearly a coordinate: put there
//  by other people's routers, indifferent to whether the fridge is running, and
//  different in every building. So this leads and RoomPrint's sound
//  corroborates, and their disagreement is reported rather than averaged away.
//
//  Two measures, kept apart because "an access point is missing" and "an access
//  point is much weaker" have different causes, and one number covering both
//  would report a mediocre match to everything:
//
//    SET     which access points are there at all, weighted by strength. It
//            separates buildings, and a place is ranked on it.
//    SIGNAL  how far the levels differ for the ones BOTH captures saw. Two
//            rooms in one house hear the same routers, but the near one reads
//            twenty decibels hotter.
//
//  The overlap is a Ruzicka index, the weighted Jaccard of sum-of-minima over
//  sum-of-maxima, with each access point weighted by its own level. A plain
//  Jaccard would count a router three streets away as heavily as the one next
//  door, and those come and go for reasons unrelated to where the device is.
//
//  Neither measure uses absolute level: both are computed against each
//  capture's own strongest few readings, because a body in the way pulls every
//  level down together and would read as a different place. The cost is that
//  two places with the same PATTERN at different strengths, a room and the
//  corridor outside it, are no longer separated by the radio. The sound usually
//  separates them, which is the argument for keeping both.
//
//  A 32-bit hash of each BSSID is stored, never the BSSID.
//
//  Hardware-free; unit-tested on the host.
// -----------------------------------------------------------------------------
#include <cstdint>

namespace sd {

/// How many access points a capture keeps: the strongest this many. A scan in a
/// town returns dozens and the tail is noise. Twelve is more than any room has
/// strong ones, and keeps a stored capture under seventy bytes.
constexpr int kRadioMaxAps = 12;

/// Below this a capture cannot identify anything. One access point is a
/// coincidence; three is a place.
constexpr int kRadioMinAps = 3;

/// How many an access point pair must share before their level difference means
/// anything. Two points make a line through any two rooms.
constexpr int kRadioMinShared = 3;

/// How many of the strongest set the level the weighting is measured against.
constexpr int kRadioTopForRef = 3;

/// A stable 32-bit name for a BSSID. Order-independent, and one way.
uint32_t bssidHash(const uint8_t* mac);

/// Whether an address is one of the rotating ones.
///
/// Here beside bssidHash because it is the same kind of fact: what an address
/// itself tells you, before anything is compared. The Bluetooth census in
/// services/RadioService.h is its caller; no print holds one.
///
/// `randomType` is whether the advertiser sent a random rather than a public
/// address, and `addrMsb` is the most significant byte of it, which under
/// NimBLE is the LAST byte as stored, that stack keeping them
/// least-significant first.
///
/// Both are needed, and this is the trap: a resolvable private address is a
/// random one whose top two bits are 0b01, but plenty of real public addresses
/// begin with a byte in that range, 0x4C being one of Apple's. Testing the byte
/// alone counts a MacBook as a rotating phone and reports a room full of
/// permanent hardware as a crowd passing through.
bool bleAddressIsPrivate(bool randomType, uint8_t addrMsb);

/// Whether a WiFi BSSID belongs to a virtual BSS rather than to a radio.
///
/// Read off the address itself, like the two above. `macMsb` is the FIRST octet
/// of the BSSID, where bit 1 marks a locally administered address. An access
/// point advertising several SSIDs from one transmitter derives the extra
/// BSSIDs by setting that bit, so they name a network rather than a radio.
///
/// Worth a function rather than a bit test at each call site because nothing is
/// ever transmitted under a virtual BSSID. The beacons announcing it carry the
/// transmitting radio's address, so anything filtering received frames by one
/// (CSI in MOTION, the RSSI hunt in RF FINDER) matches nothing at all. That
/// failure is silent in both: a flat reading looks like a quiet room, and a
/// hunt that never registers looks like a device out of range.
bool wifiAddressIsVirtual(uint8_t macMsb);

/// The order MOTION offers access points in: real radios first, then virtual
/// ones, each group strongest first. Writes a permutation of 0..n-1 into
/// `order` and returns how many it wrote.
///
/// Here rather than in the app because the output is always plausible: a list
/// is a list whichever order it comes out in. Two of the three rules are easy
/// to get subtly wrong. The groups cannot be sorted in one pass with
/// virtual-ness as a tiebreak, because level is then the outer key and a loud
/// virtual BSSID climbs above a real radio; and a stable sort is what keeps two
/// access points at the same level in the order the scan found them, rather
/// than swapping places while somebody is trying to press one.
///
/// `rssi` and `isVirtual` are parallel arrays of `n` candidates, already
/// reduced to the WiFi ones.
int orderCsiCandidates(const int8_t* rssi, const bool* isVirtual, int n,
                       uint8_t* order, int cap);

/// One place's radio view: the strongest access points it could hear.
struct RadioPrint {
    /// Sorted ascending, so two prints can be walked in step.
    uint32_t id[kRadioMaxAps]{};
    /// As measured, in dBm. Stored absolute because that is what was seen; the
    /// comparisons below decide how much of it to use.
    int8_t  rssi[kRadioMaxAps]{};
    uint8_t count = 0;
    bool    valid = false;
};

/// What an access point is worth in the comparison, given how it compares with
/// the strongest one in the same capture.
///
/// Full weight at the top and near nothing forty decibels below, because the
/// far ones come and go on their own. Never quite zero: an access point that
/// appeared should count for something.
///
/// Relative to the capture rather than absolute, so a body in the way or a
/// different holding angle, both of which pull EVERY level down together, does
/// not change which access points the comparison leans on.
float radioWeight(int rssiDbm, int referenceDbm);

/// The level the weighting is measured against: the mean of the strongest few.
///
/// Not the single strongest, which is one access point away from being wrong:
/// switch off the router the reference rests on and every other weight moves
/// with it. Measured across three choices on synthetic rooms, the mean of the
/// top three held a revisit tighter and left a wider gap to the room next door;
/// the median normalised so hard that an adjoining room scored HIGHER than a
/// genuine second visit.
int radioReferenceDbm(const RadioPrint& p);

/// Reduces a scan to a print: keeps the strongest kRadioMaxAps and sorts them.
/// Invalid, rather than empty, when too few answered.
bool buildRadioPrint(const uint32_t* ids, const int8_t* rssi, int n,
                     RadioPrint& out);

/// Weighted overlap of two prints, 1 for identical and 0 for nothing in common.
float radioSetSimilarity(const RadioPrint& a, const RadioPrint& b);

/// Weighted r.m.s. level difference over the access points both saw, in dB,
/// after each capture's own mean over those access points has been removed.
///
/// The mean is removed for the reason RoomPrint removes it from a spectrum: it
/// turns a set of levels into a PATTERN, and the pattern belongs to the room. A
/// body between the device and the routers moves the mean, not the pattern.
///
/// `shared` receives how many were common. Returns 0 when fewer than
/// kRadioMinShared were, which is no answer rather than a small difference:
/// callers must check `shared`, or a place in another building will look like a
/// perfect level match.
float radioSignalDistance(const RadioPrint& a, const RadioPrint& b, int* shared);

// -----------------------------------------------------------------------------
enum class RadioAspect : uint8_t { Set, Signal, Count };
constexpr int kRadioAspectCount = static_cast<int>(RadioAspect::Count);

const char* radioAspectName(RadioAspect a);

enum class RadioVerdict : uint8_t {
    Unmeasured,   ///< the scan itself was not usable
    NoReference,  ///< nothing learned here yet
    TooFew,       ///< a place exists but is too thin to have a scatter
    Match,        ///< the same access points in the same pattern
    Unsure,       ///< between the thresholds; no statement worth making
    Different,    ///< a different set of access points
};

const char* radioVerdictName(RadioVerdict v);

/// One learned place, as the radio sees it.
///
/// The scatter is measured differently from RoomReference's, because the
/// feature is not a vector that can be averaged: two captures have no mean AP
/// set. Instead the place's own visits are compared with EACH OTHER, and the
/// spread of those pairwise agreements is what a new capture is judged against.
class RadioReference {
public:
    static constexpr int kMaxCaptures = 4;
    /// Two captures make one pair, and one pair has no scatter.
    static constexpr int kMinCaptures = 3;

    /// Floors on the scatter, in the units of each measure.
    ///
    /// Reasoned rather than measured, and generous in the same direction as
    /// RoomReference's: a floor set too high says "unsure", which is
    /// recoverable, and one set too low says "somewhere else" about the right
    /// room, which is not.
    static constexpr float kSetSpreadFloor      = 0.06f;
    static constexpr float kSignalSpreadFloorDb = 3.0f;

    static constexpr float kMatchSigma  = 1.5f;
    static constexpr float kDifferSigma = 2.5f;

    void clear();
    bool add(const RadioPrint& p);

    int  count() const { return _n; }
    bool ready() const { return _n >= kMinCaptures; }
    const RadioPrint& at(int i) const;

    /// What this place's own visits agree with each other to, and how much
    /// that agreement varies.
    float selfMean(RadioAspect a) const;
    float selfSpread(RadioAspect a) const;
    float usedSpread(RadioAspect a) const;

    /// How far `p` sits from this place, in units of the place's own scatter.
    /// Zero when it is at least as close as the place's visits are to each
    /// other, and zero when the reference cannot support the question.
    float z(RadioAspect a, const RadioPrint& p) const;

    /// Whether the level comparison has enough shared access points to mean
    /// anything against this place.
    bool signalUsable(const RadioPrint& p) const;

    /// The level this place has heard `id` at, averaged over the visits that
    /// heard it at all, and how many of them that was. False when none did.
    ///
    /// For a screen: the grey line the live bars are read against, and the only
    /// way to see WHY a capture scored what it did.
    bool meanFor(uint32_t id, float& dbm, int& seen) const;

    /// Access points this place usually hears, a majority of its visits having
    /// seen them, that `p` did not. Bars that are not there carry as much as
    /// the ones that are.
    int missingFrom(const RadioPrint& p) const;

    /// What the two measures together say about `p`.
    ///
    /// There is deliberately no "same building, different corner" verdict. It
    /// was written and then measured: a pattern change large enough to fail the
    /// level test always moves the SET score with it, because the weights come
    /// from the levels, and somewhere else in the same house comes out at nine
    /// or ten sigma on the set alone.
    RadioVerdict judge(const RadioPrint& p) const;

private:
    /// Similarity or distance between two captures, by aspect.
    static float measure(RadioAspect a, const RadioPrint& x, const RadioPrint& y,
                         bool* usable);

    RadioPrint _captures[kMaxCaptures]{};
    int        _n = 0;
};

/// Which of `places` this capture belongs to, or -1 if none can answer.
///
/// Ranked on the SET, which is the identity. `margin` receives how far ahead of
/// the runner-up the winner is, in the same units, so a caller can decline to
/// name a room that only just won.
int bestRadioPlace(const RadioReference* places, int n, const RadioPrint& p,
                   float* bestZ, float* margin);

/// Reported when only one place was in the running: nothing was beaten, so no
/// finite number describes the lead. Matches kRoomLoneMargin's reasoning.
constexpr float kRadioLoneMargin = 1000.0f;

/// Below this the winner and the runner-up are not separable by this capture.
constexpr float kRadioAmbiguousMargin = 0.75f;

}  // namespace sd
