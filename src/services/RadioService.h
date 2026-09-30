#pragma once
// -----------------------------------------------------------------------------
//  RadioService - 2.4 GHz, for finding things.
//
//  Two ways to get a stream of RSSI readings out of hardware you do not control:
//
//    WiFi   Scan for access points, then lock the radio to one channel in
//           promiscuous mode and count every frame from a chosen BSSID. Beacons
//           alone give about 10 readings a second, and a busy AP far more. Far
//           better than repeated scanning, which spends most of its time on
//           channels the target is not on.
//
//    BLE    Passive advertisement scanning. Trackers, tags, bands and
//           headphones all announce themselves, at a rate they choose,
//           typically between half a hertz and ten.
//
//  Only one runs at a time. They share one 2.4 GHz front end, and asking for
//  both makes the hardware time-slice, halving each one's update rate.
//
//  beginAp() puts the same radio into soft-AP mode so a phone can join the
//  device directly. It is here rather than in the app that wants it because
//  this class is what Cap::WiFi acquires and releases, so an access point left
//  running when its app closes cannot arise. Nothing scans or hunts while it is
//  up: the radio is serving a network, not surveying one.
// -----------------------------------------------------------------------------
#include <esp_wifi_types.h>

#include <atomic>
#include <cstddef>
#include <cstdint>

/// Forward-declared rather than included: the Bluetooth headers are large and
/// only the implementation needs them, so every file wanting a RadioService
/// would otherwise pay for the whole stack.
class BLEAdvertisedDevice;

namespace sd {

void radioPromiscuousCb(void* buf, wifi_promiscuous_pkt_type_t type);
void radioCsiCb(void* ctx, wifi_csi_info_t* info);

/// One packet's channel state information, copied out of the driver's buffer.
///
/// 384 bytes covers everything up to 40 MHz HT without STBC; the 612-byte STBC
/// layout is truncated, which is harmless because the metric only compares a
/// capture with the one before it.
struct CsiFrame {
    static constexpr int kMaxBytes = 384;

    int8_t   data[kMaxBytes]{};
    uint16_t len              = 0;
    bool     firstWordInvalid = false;
    int8_t   rssi             = -127;
    uint32_t atMs             = 0;
};

/// A count of the Bluetooth population, kept alongside the target list.
///
/// The target list caps at kMaxTargets, and for a crowd that cap is the answer
/// rather than a limit on it: twenty-four makes a train indistinguishable from
/// a cafe. So this counts instead of listing: distinct devices exactly up to
/// kSlots, their strongest levels, how many were rotating private addresses,
/// how many announced a name, and a tally per company identifier.
///
/// No address is kept. What identifies a device here is a 16-bit fingerprint of
/// an address that itself rotates every quarter of an hour; it exists only to
/// avoid counting one phone forty times and never leaves RAM. A list of
/// addresses would be a record of the people standing nearby rather than a
/// measurement of the room.
///
/// Written from the Bluetooth host task and read from the main loop with no
/// lock. Reading it mid-scan is safe: each device's level is stored before its
/// slot is claimed, so a reader never sees a claimed slot without a level. Such
/// a read can be a device or two behind, which is what a live preview wants.
/// The capture itself is taken after scanBleStop(), with no writer left.
struct BleCensus {
    static constexpr int kSlots   = 256;
    static constexpr int kVendors = 24;

    uint32_t adverts     = 0;  ///< advertisements seen, repeats included
    uint16_t devices     = 0;  ///< distinct, by fingerprint
    uint16_t rpa         = 0;  ///< of those, rotating private addresses
    uint16_t named       = 0;  ///< of those, that announced a name
    uint16_t vendorCount = 0;

    uint16_t vendorId[kVendors]{};
    uint16_t vendorHits[kVendors]{};

    uint16_t fp[kSlots]{};    ///< 0 is an empty slot
    int8_t   best[kSlots]{};  ///< strongest level heard from that device

    /// More devices than kSlots answered, so `devices` is a floor rather than a
    /// count. Said out loud, because a saturated counter that looks like a
    /// measurement is the failure this structure replaces.
    bool full = false;
};

struct RadioTarget {
    uint8_t mac[6]{};
    char    name[24]{};
    int8_t  rssi    = -127;
    uint8_t channel = 0;
    bool    isBle   = false;
    bool    open    = false;  ///< WiFi only: unencrypted
    /// WiFi only: an extra SSID sharing another radio's transmitter. Nothing is
    /// ever sent under this address, so it can be listed but never listened to;
    /// see mathx/RadioPrint.h. Decided once here rather than by each app,
    /// because an app that does not know to ask gets a target that is silent
    /// for a reason it cannot see.
    bool    virtualBss = false;
};

class RadioService {
public:
    static constexpr int kMaxTargets = 24;

    // -- lifecycle -----------------------------------------------------------
    bool beginWifi();
    bool beginBle();

    /// Brings up an access point with no uplink of any kind.
    ///
    /// `pass` shorter than eight characters is refused by WPA2, so anything
    /// under that opens the network instead, stated rather than silently
    /// downgraded. A channel is chosen rather than left to the driver, so the
    /// number can go on the screen next to the name.
    bool beginAp(const char* ssid, const char* pass, uint8_t channel = 6);
    void end();

    bool wifiUp() const { return _wifi; }
    bool bleUp() const  { return _ble; }
    bool apUp() const   { return _ap; }

    /// How many stations are associated. Zero is the useful case: the
    /// difference between "the phone has not joined yet" and "it joined and the
    /// page is not loading", which are fixed in different places.
    int apClients() const;
    /// The address the page is served on, for putting on the screen. Empty
    /// until the access point is up.
    const char* apAddress() const { return _apIp; }

    // -- discovery -----------------------------------------------------------
    /// Blocking WiFi scan across all channels. Returns the number found.
    int scanWifi(uint32_t msPerChannel = 120);

    /// Asynchronous version. A full 2.4 GHz sweep takes well over a second, and
    /// blocking that long stops the screen redrawing and the buttons answering
    /// for the whole sweep.
    bool scanWifiStart(uint32_t msPerChannel = 120);
    /// True on the poll where a scan finished and the target list was refreshed.
    bool scanWifiPoll();
    bool scanBusy() const { return _asyncScan; }

    /// Starts (or restarts) a BLE passive scan that runs in the background.
    void scanBleStart();

    /// The population heard since the last resetBleCensus().
    const BleCensus& bleCensus() const { return _census; }
    void             resetBleCensus();

    /// How long one Bluetooth scan may run.
    ///
    /// Not a taste: the framework's scanner keeps every device it has seen in
    /// an unbounded cache and re-appends the payload of each repeated
    /// advertisement, so heap and per-advertisement work both climb with the
    /// length of a scan. Restarting the scan reclaims it. A capture wants a few
    /// seconds; this is the ceiling.
    static constexpr uint32_t kBleCensusMaxMs = 15000;
    void scanBleStop();

    int                count() const { return _count; }
    const RadioTarget& at(int i) const { return _targets[i]; }
    void               clearTargets();

    // -- hunting -------------------------------------------------------------
    /// Locks the WiFi radio to `channel` and counts frames from `bssid`.
    bool huntWifi(const uint8_t* bssid, uint8_t channel);
    void huntStop();

    /// Pops one RSSI sample captured since the last call. False when the queue
    /// is empty. The producer is the WiFi driver's own task, so this is a
    /// single-producer single-consumer ring rather than anything locked.
    bool popSample(int& rssiDbm, uint32_t& atMs);

    /// BLE hunting reads from the same queue; the scan callback fills it.
    void huntBle(const uint8_t* mac);

    // -- channel state information -------------------------------------------
    /// Park on `channel` in sniffer mode and collect CSI from `bssid`.
    /// Park on `channel` in sniffer mode and collect CSI from `bssid`.
    ///
    /// Sniffer rather than station mode, on Espressif's advice: a station
    /// receives nothing while disconnected and only the AP's traffic once
    /// associated, where a sniffer parked on a channel gets every beacon on it,
    /// about ten a second from a transmitter that is not moving. It also needs
    /// no credentials for a network we have no business joining.
    bool beginCsi(const uint8_t* bssid, uint8_t channel);
    void endCsi();
    bool csiUp() const { return _csi.load(std::memory_order_acquire); }

    /// Take the oldest captured frame. False when none is waiting.
    bool popCsi(CsiFrame& out);

    /// Frames the consumer was too slow to collect. Shown rather than hidden: a
    /// detector fed an unknown fraction of the traffic is not measuring what it
    /// claims to be.
    uint32_t csiDropped() const { return _csiDropped.load(std::memory_order_relaxed); }

private:
    friend void radioPromiscuousCb(void* buf, wifi_promiscuous_pkt_type_t type);
    friend void radioCsiCb(void* ctx, wifi_csi_info_t* info);
    friend class RadioBleCallbacks;

    /// Stops everything the radio is DOING, without powering it down.
    ///
    /// Split out of end() because the difference is a race. WiFi.mode() only
    /// calls espWiFiStop() on the way to WIFI_OFF, and the next mode() then
    /// re-initialises the driver, so OFF-then-AP lands the AP_START event while
    /// the stop is still settling and the handler gives up with
    /// ESP_ERR_WIFI_STOP_STATE before reaching esp_netif_action_start(). The
    /// network appears with no interface behind it. Switching straight from STA
    /// to AP never stops the driver at all.
    void quiesce();

    void pushSample(int rssi, uint32_t atMs);
    /// Copies a finished WiFi scan into the target list, for both scan paths.
    void takeScanResults(int found);
    int  findOrAdd(const uint8_t* mac);
    void censusAdd(const uint8_t* mac, uint8_t addrType, BLEAdvertisedDevice& dev);
    void countVendor(uint16_t company);

    // -------------------------------------------------------------------------
    //  Two rings, and why they are atomic rather than volatile
    //
    //  The producer of each is a driver callback on the WiFi or Bluetooth task
    //  and the consumer is the app loop, and on a dual-core S3 those are not
    //  the same core. `volatile` stops the compiler caching a value in a
    //  register and does nothing else: it orders nothing, so a payload written
    //  before the index that publishes it can still become visible after it.
    //  The consumer then reads a slot the producer claimed but did not fill,
    //  and gets whatever was there before. Stale CSI is a plausible reading of
    //  a still room, which for a detector is the worst kind of wrong.
    //
    //  So each index is released by the side that owns it and acquired by the
    //  side that does not:
    //
    //    producer: fill the slot, then STORE its index with release
    //    consumer: ACQUIRE that index, then read the slot
    //
    //  which makes the fill happen-before the read. An index is only ever
    //  written by one side, so that side reads its own with relaxed. On this
    //  target both compile to a plain 32-bit load or store plus a barrier.
    //
    //  Loads and stores only, never fetch_add, exchange or compare_exchange.
    //  The Arduino build passes -mdisable-hardware-atomics, so any
    //  read-modify-write becomes a call to libatomic, which implements it by
    //  disabling interrupts. Taking a global critical section inside a driver
    //  callback is what this arrangement exists to avoid, and nothing about it
    //  shows up as a warning. Every index and counter here has a single writer.
    //
    //  `_hunting`, `_csi` and `_scanning` are the gates on all of this and are
    //  atomic for the same reason: each is set after its ring is reset, so the
    //  release store on the flag is what publishes the reset.
    // -------------------------------------------------------------------------
    static constexpr int kQueue = 64;
    struct Sample { int16_t rssi; uint32_t atMs; };

    std::atomic<uint32_t> _head{0};   ///< written by the producer only
    std::atomic<uint32_t> _tail{0};   ///< written by the consumer only
    Sample                _queue[kQueue]{};

    RadioTarget _targets[kMaxTargets]{};
    int         _count = 0;

    /// Static rather than allocated on demand: it is meaningful only during a
    /// scan, but a lifetime bug in a structure written from another task would
    /// cost more than the bytes do on a board with eight megabytes of PSRAM.
    BleCensus _census{};

    // The CSI callback runs in the WiFi task, so it copies and leaves. Anything
    // longer there stalls the driver feeding it.
    static constexpr int kCsiQueue = 4;
    CsiFrame              _csiQueue[kCsiQueue]{};
    std::atomic<uint32_t> _csiHead{0};      ///< written by the producer only
    std::atomic<uint32_t> _csiTail{0};      ///< written by the consumer only
    /// Producer-only, and read for display. Nothing is ordered against it, so
    /// relaxed: a count that lands a frame late is still the right count.
    std::atomic<uint32_t> _csiDropped{0};
    std::atomic<bool>     _csi{false};

    uint8_t           _huntMac[6]{};
    std::atomic<bool> _hunting{false};
    bool              _wifi    = false;
    bool              _ble     = false;
    bool              _ap      = false;
    char              _apIp[16]{};
    std::atomic<bool> _scanning{false};
    bool              _asyncScan = false;
};

}  // namespace sd
