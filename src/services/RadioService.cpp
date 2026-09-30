#include "services/RadioService.h"

#include "core/Log.h"

#include "mathx/RadioPrint.h"

#include <Arduino.h>
#include <WiFi.h>
#include <esp_wifi.h>

#include <BLEDevice.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>

#include <cstdio>
#include <cstring>

namespace sd {

namespace {
RadioService* g_radio = nullptr;

bool macEqual(const uint8_t* a, const uint8_t* b) {
    return std::memcmp(a, b, 6) == 0;
}

/// A short name for an address, only so one device heard forty times is counted
/// once. Never stored, never shown, one-way.
uint16_t addrFingerprint(const uint8_t* mac) {
    uint32_t h = 2166136261u;
    for (int i = 0; i < 6; ++i) {
        h ^= mac[i];
        h *= 16777619u;
    }
    const uint16_t f = static_cast<uint16_t>((h >> 16) ^ (h & 0xFFFFu));
    return f ? f : 1u;  // 0 marks an empty slot
}

/// Both Bluetooth stacks number the address types the same way for the cases
/// that matter here, 0 public and 1 random with the identity-resolved variants
/// 2 and 3 alongside, so the low bit is "random" whichever host is compiled in.
/// Tested on the bit rather than an enum name, because the names differ.
bool addrTypeIsRandom(uint8_t type) { return (type & 0x01u) != 0u; }

}  // namespace

// -----------------------------------------------------------------------------
//  Promiscuous receive. Runs in the WiFi driver's task, so it does the minimum
//  and never blocks: filter by MAC, drop a timestamped RSSI into a ring.
// -----------------------------------------------------------------------------
void radioPromiscuousCb(void* buf, wifi_promiscuous_pkt_type_t type) {
    // CSI installs this callback too and leaves _hunting false. Bail before
    // doing per-packet work for a consumer that is not listening.
    if (!g_radio || !g_radio->_hunting.load(std::memory_order_acquire)) return;
    if (type != WIFI_PKT_MGMT && type != WIFI_PKT_DATA) return;

    auto* pkt = static_cast<wifi_promiscuous_pkt_t*>(buf);
    // 802.11 header: addr2 (transmitter) starts at offset 10.
    const uint8_t* addr2 = pkt->payload + 10;
    if (!macEqual(addr2, g_radio->_huntMac)) return;

    g_radio->pushSample(pkt->rx_ctrl.rssi, millis());
}

void RadioService::pushSample(int rssi, uint32_t atMs) {
    // Our own index needs no ordering; the consumer's does, or "not full" could
    // be decided on a tail it has already moved.
    const uint32_t head = _head.load(std::memory_order_relaxed);
    const uint32_t next = (head + 1) % kQueue;
    if (next == _tail.load(std::memory_order_acquire)) return;  // full: drop the newest
    _queue[head] = Sample{static_cast<int16_t>(rssi), atMs};
    // Release last: the sample is in the slot before the index says so.
    _head.store(next, std::memory_order_release);
}

bool RadioService::popSample(int& rssiDbm, uint32_t& atMs) {
    const uint32_t tail = _tail.load(std::memory_order_relaxed);
    // Acquire, so the sample this index admits to is the one actually written.
    if (tail == _head.load(std::memory_order_acquire)) return false;
    rssiDbm = _queue[tail].rssi;
    atMs    = _queue[tail].atMs;
    // Release last: the slot is free only once it has been read out of.
    _tail.store((tail + 1) % kQueue, std::memory_order_release);
    return true;
}

// -----------------------------------------------------------------------------
class RadioBleCallbacks : public BLEAdvertisedDeviceCallbacks {
public:
    void onResult(BLEAdvertisedDevice dev) override {
        if (!g_radio) return;

        // Copied, not pointed at. getAddress() returns by value and getNative()
        // hands back the inside of that temporary, which dies at the end of the
        // full expression, so a pointer to it only ever worked by luck.
        BLEAddress addr = dev.getAddress();  // getNative() is not const
        uint8_t          mac[6];
        std::memcpy(mac, addr.getNative(), sizeof(mac));

        if (g_radio->_hunting.load(std::memory_order_acquire) &&
            macEqual(mac, g_radio->_huntMac)) {
            g_radio->pushSample(dev.getRSSI(), millis());
        }
        if (!g_radio->_scanning.load(std::memory_order_acquire)) return;

        g_radio->censusAdd(mac, addr.getType(), dev);

        const int idx = g_radio->findOrAdd(mac);
        if (idx < 0) return;
        RadioTarget& t = g_radio->_targets[idx];
        t.rssi  = static_cast<int8_t>(dev.getRSSI());
        t.isBle = true;
        if (t.name[0] == '\0') {
            if (dev.haveName() && dev.getName().length() > 0) {
                std::strncpy(t.name, dev.getName().c_str(), sizeof(t.name) - 1);
            } else {
                std::snprintf(t.name, sizeof(t.name), "%02X:%02X:%02X:%02X:%02X:%02X",
                              mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
            }
        }
    }
};

namespace {
RadioBleCallbacks g_bleCb;
BLEScan*          g_scan = nullptr;
}  // namespace

// -----------------------------------------------------------------------------
bool RadioService::beginWifi() {
    if (_wifi) return true;
    // Quiesced rather than powered down, for the reason in quiesce(): a start
    // that lands on top of a stop leaves the interface unstarted.
    quiesce();
    if (_ap) {
        WiFi.softAPdisconnect(false);
        _apIp[0] = '\0';
        _ap      = false;
    }
    g_radio = this;

    // Checked, because it can fail: esp_wifi_set_mode stores the mode in NVS,
    // so a full partition takes the radio down with it and every later call
    // reports something further downstream.
    if (!WiFi.mode(WIFI_STA)) {
        SD_LOGW("radio", "wifi mode refused - nvs full?");
        g_radio = nullptr;
        return false;
    }
    // There is deliberately no WiFi.disconnect() here, and no WiFi.begin()
    // anywhere in this firmware. Scanning and sniffing need the radio up, not a
    // link: associating would put us on an AP's traffic schedule instead of
    // ours, and without begin() the station never associates at all.
    //
    // `WiFi.disconnect(false, true)` used to sit here, its second argument
    // being `eraseap`. Neither half did anything. esp_wifi_disconnect() on a
    // station that never connected is a no-op, and the erase branch begins
    //
    //     if (!started()) { log_e("STA not started! You must call begin first."); }
    //
    // where started() is ESP_NETIF_STARTED_BIT, set by begin() and nothing
    // else. So it logged that error on every radio app that opened and cleared
    // nothing, which was fine, because there was nothing to clear. That line is
    // not a symptom of anything: not the NVS exhaustion it was filed under, and
    // the WiFi.mode() check above did not silence it.
    // unconditionally, all along.
    delay(50);

    _wifi = true;
    SD_LOGI("radio", "wifi up");
    return true;
}

bool RadioService::beginAp(const char* ssid, const char* pass, uint8_t channel) {
    if (_ap) return true;
    // Quiesced, NOT powered down. beginWifi() has already run if the app
    // declared Cap::WiFi, so arriving here in STA mode is the ordinary path.
    quiesce();
    _wifi   = false;   // STA is being replaced here, not torn down
    g_radio = this;

    // WPA2 needs eight characters. Passing fewer does not fail loudly, it opens
    // the network, so the decision is made here where it can be reported.
    const bool secured = pass && std::strlen(pass) >= 8;

    if (!WiFi.mode(WIFI_AP)) {
        SD_LOGW("radio", "ap mode refused - nvs full?");
        g_radio = nullptr;
        return false;
    }
    // No uplink and nothing to route to: this network exists so one phone can
    // reach one page on this device.
    const bool ok = WiFi.softAP(ssid, secured ? pass : nullptr, channel,
                                /*ssid_hidden*/ 0, /*max_connection*/ 4);
    if (!ok) {
        SD_LOGW("radio", "softAP refused");
        WiFi.mode(WIFI_OFF);
        g_radio = nullptr;
        return false;
    }

    const IPAddress ip = WiFi.softAPIP();
    std::snprintf(_apIp, sizeof(_apIp), "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);

    _ap = true;
    SD_LOGI("radio", "ap up: %s ch%u %s at %s", ssid, (unsigned)channel,
            secured ? "wpa2" : "OPEN", _apIp);
    return true;
}

int RadioService::apClients() const {
    return _ap ? WiFi.softAPgetStationNum() : 0;
}

bool RadioService::beginBle() {
    if (_ble) return true;
    // This one really does power the radio down: BLE and WiFi share the front
    // end, and nothing starts WiFi straight afterwards, so there is no start to
    // race the stop.
    end();
    g_radio = this;

    BLEDevice::init("");
    g_scan = BLEDevice::getScan();
    g_scan->setAdvertisedDeviceCallbacks(&g_bleCb, /*wantDuplicates*/ true);
    // Passive: listen only. Active scanning would transmit scan requests, which
    // costs power and announces us to everything in the room.
    g_scan->setActiveScan(false);
    g_scan->setInterval(80);
    g_scan->setWindow(70);

    _ble = true;
    SD_LOGI("radio", "ble up");
    return true;
}

void RadioService::quiesce() {
    // Before huntStop drops promiscuous mode, so the driver is told to stop
    // producing CSI rather than left with a callback into a dead consumer.
    endCsi();
    huntStop();
    if (_ble) {
        scanBleStop();
        // false, and it is the whole difference between a radio that comes back
        // and one that does not. deinit(true) calls btMemRelease(), which hands
        // the controller's memory to the heap for good; the framework's own
        // comment says "this prevents reinitialization". The next
        // BLEDevice::init() then fails inside esp_bt_controller_init and its
        // failure path frees a semaphore that now lives in reused heap:
        //
        //     CORRUPT HEAP: Bad head at ... multi_heap_free
        //     btdm_controller_deinit_internal <- btdm_controller_init
        //
        // The controller's memory is static BSS whether or not it is released,
        // so keeping it costs nothing that was ever available to anything else.
        BLEDevice::deinit(false);
        g_scan = nullptr;
        _ble   = false;
        SD_LOGI("radio", "ble down");
    }
    if (_wifi) {
        // A scan left in flight is not just a stale flag. scanWifiStart()
        // refuses while _asyncScan is set, so it never reaches the scanDelete()
        // that would clear the driver's own scanning bit, and the
        // arduino-esp32 layer then reports WIFI_SCAN_RUNNING until its
        // sixty-second timeout. MOTION sits with a scan in flight until it has
        // picked an illuminator, so backing out mid-scan is the ordinary case.
        if (_asyncScan || _scanning) WiFi.scanDelete();
        _asyncScan = false;
        _scanning.store(false, std::memory_order_release);

        esp_wifi_set_promiscuous(false);
    }
}

void RadioService::end() {
    quiesce();
    if (_ap) {
        // Disconnects the stations first. Dropping straight to WIFI_OFF leaves
        // a phone believing it is still associated, sitting on a dead network
        // rather than falling back to one that works.
        //
        // false, because the power-down is the explicit mode() below. Passing
        // true routes through AP.end() -> enableAP(false) -> mode(WIFI_OFF),
        // which would do it as a side effect and then again here.
        WiFi.softAPdisconnect(false);
        _apIp[0] = '\0';
    }
    // One power-down, for whichever was up, and the only place it happens.
    // Every other path switches mode instead; see quiesce().
    if (_ap || _wifi) {
        SD_LOGI("radio", _ap ? "ap down" : "wifi down");
        WiFi.mode(WIFI_OFF);
    }
    _ap     = false;
    _wifi   = false;
    g_radio = nullptr;
}

void RadioService::clearTargets() {
    _count = 0;
    std::memset(_targets, 0, sizeof(_targets));
}

int RadioService::findOrAdd(const uint8_t* mac) {
    for (int i = 0; i < _count; ++i) {
        if (macEqual(_targets[i].mac, mac)) return i;
    }
    if (_count >= kMaxTargets) return -1;
    const int i = _count++;
    std::memcpy(_targets[i].mac, mac, 6);
    _targets[i].name[0] = '\0';
    return i;
}

/// Copies a finished WiFi scan into the target list.
///
/// Written out once rather than identically in scanWifi() and scanWifiPoll():
/// the blocking scan and the asynchronous one differ in how they wait, not in
/// what a result means. A field added to RadioTarget in one and not the other
/// is a target that behaves differently depending on which app asked.
void RadioService::takeScanResults(int found) {
    for (int i = 0; i < found && _count < kMaxTargets; ++i) {
        const uint8_t* bssid = WiFi.BSSID(i);
        if (!bssid) continue;
        const int idx = findOrAdd(bssid);
        if (idx < 0) break;
        RadioTarget& t = _targets[idx];
        t.rssi       = static_cast<int8_t>(WiFi.RSSI(i));
        t.channel    = static_cast<uint8_t>(WiFi.channel(i));
        t.isBle      = false;
        t.open       = WiFi.encryptionType(i) == WIFI_AUTH_OPEN;
        t.virtualBss = wifiAddressIsVirtual(bssid[0]);
        const String ssid = WiFi.SSID(i);
        if (ssid.length() > 0) {
            std::strncpy(t.name, ssid.c_str(), sizeof(t.name) - 1);
        } else {
            std::snprintf(t.name, sizeof(t.name), "<hidden %02X%02X>", bssid[4], bssid[5]);
        }
    }
}

int RadioService::scanWifi(uint32_t msPerChannel) {
    if (!_wifi) return 0;
    esp_wifi_set_promiscuous(false);
    clearTargets();

    const int found = WiFi.scanNetworks(false, /*show_hidden*/ true, /*passive*/ false,
                                        msPerChannel);
    takeScanResults(found);
    WiFi.scanDelete();
    SD_LOGI("radio", "wifi scan: %d", _count);
    return _count;
}

bool RadioService::scanWifiStart(uint32_t msPerChannel) {
    if (!_wifi || _asyncScan) return false;
    esp_wifi_set_promiscuous(false);
    WiFi.scanDelete();
    const int r = WiFi.scanNetworks(/*async*/ true, /*hidden*/ true,
                                    /*passive*/ false, msPerChannel);
    _asyncScan = (r == WIFI_SCAN_RUNNING);
    return _asyncScan;
}

bool RadioService::scanWifiPoll() {
    if (!_asyncScan) return false;
    const int found = WiFi.scanComplete();
    if (found == WIFI_SCAN_RUNNING) return false;

    _asyncScan = false;
    if (found < 0) {
        SD_LOGW("radio", "scan refused (%d)", found);
        clearTargets();
        return true;
    }
    clearTargets();
    takeScanResults(found);
    WiFi.scanDelete();
    SD_LOGI("radio", "scan: %d seen, %d kept", found, _count);
    return true;
}

void RadioService::scanBleStart() {
    if (!_ble || !g_scan) return;
    clearTargets();
    _scanning.store(true, std::memory_order_release);
    g_scan->start(0, nullptr, false);  // 0 = run until stopped
}

void RadioService::resetBleCensus() { _census = BleCensus{}; }

/// Folds one advertisement into the population count.
///
/// Runs on the Bluetooth host task, so it stays arithmetic: an open-addressed
/// probe, a couple of counters, no allocation. Anything slower stalls the stack
/// feeding it.
void RadioService::censusAdd(const uint8_t* mac, uint8_t addrType,
                             BLEAdvertisedDevice& dev) {
    ++_census.adverts;

    const uint16_t f = addrFingerprint(mac);
    const int      rssi = dev.getRSSI();

    int slot = static_cast<int>(f) % BleCensus::kSlots;
    for (int probe = 0; probe < BleCensus::kSlots; ++probe) {
        uint16_t& cell = _census.fp[slot];

        if (cell == f) {  // heard before: keep the strongest and nothing else
            if (rssi > _census.best[slot]) {
                _census.best[slot] = static_cast<int8_t>(rssi);
            }
            return;
        }

        if (cell == 0) {  // new device
            // Level first, slot second. The main loop reads this table while
            // the scan runs, to draw the grains arriving, and decides a slot is
            // occupied by `fp` being non-zero. Claiming the slot first would
            // let it read a device at zero dBm and draw the grain wrongly.
            _census.best[slot] = static_cast<int8_t>(rssi);
            cell = f;
            ++_census.devices;

            // The MSB of the address, which under this build's Bluetooth host
            // is the LAST byte of the stored six; see BLEAddress's own note
            // that its bytes are in inverse order. Classified with the type as
            // well, because the byte alone calls a public address beginning
            // 0x4C a rotating one, and 0x4C is Apple's.
            if (bleAddressIsPrivate(addrTypeIsRandom(addrType), mac[5])) {
                ++_census.rpa;
            }
            if (dev.haveName() && dev.getName().length() > 0) ++_census.named;

            if (dev.haveManufacturerData()) {
                const String md = dev.getManufacturerData();
                // .length(), never strlen: the payload is binary and may hold a
                // zero byte before the end of it.
                if (md.length() >= 2) {
                    const uint16_t company =
                        static_cast<uint16_t>(static_cast<uint8_t>(md[0])) |
                        static_cast<uint16_t>(static_cast<uint8_t>(md[1])) << 8;
                    countVendor(company);
                }
            }
            return;
        }

        slot = (slot + 1) % BleCensus::kSlots;
    }

    // Every slot taken. The count stops being a count and says so, rather than
    // quietly reporting the ceiling as the size of the crowd.
    _census.full = true;
}

void RadioService::countVendor(uint16_t company) {
    for (int i = 0; i < _census.vendorCount; ++i) {
        if (_census.vendorId[i] == company) {
            ++_census.vendorHits[i];
            return;
        }
    }
    if (_census.vendorCount >= BleCensus::kVendors) return;
    _census.vendorId[_census.vendorCount]   = company;
    _census.vendorHits[_census.vendorCount] = 1;
    ++_census.vendorCount;
}

void RadioService::scanBleStop() {
    _scanning.store(false, std::memory_order_release);
    if (g_scan) g_scan->stop();
}

// -----------------------------------------------------------------------------
bool RadioService::huntWifi(const uint8_t* bssid, uint8_t channel) {
    if (!_wifi || !bssid) return false;
    // Gate first, then empty: a hunt already running has a producer that would
    // otherwise be writing through the indices as they are reset.
    _hunting.store(false, std::memory_order_release);
    std::memcpy(_huntMac, bssid, 6);
    _head.store(0, std::memory_order_relaxed);
    _tail.store(0, std::memory_order_relaxed);

    esp_wifi_set_promiscuous(false);
    wifi_promiscuous_filter_t filter{};
    filter.filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT | WIFI_PROMIS_FILTER_MASK_DATA;
    esp_wifi_set_promiscuous_filter(&filter);
    esp_wifi_set_promiscuous_rx_cb(&radioPromiscuousCb);
    if (esp_wifi_set_promiscuous(true) != ESP_OK) {
        SD_LOGW("radio", "promiscuous mode refused");
        return false;
    }
    // Parking on the target's channel is what turns a 0.3 Hz scan into a 10 Hz
    // stream: every beacon it sends is now a reading.
    //
    // A channel the country setting does not allow is refused here and the
    // radio stays where it was, so the hunt would listen on the wrong channel
    // and report a target that had simply stopped transmitting. Say so and give
    // up instead, putting the sniffer back down on the way out.
    const esp_err_t ch = esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
    if (ch != ESP_OK) {
        SD_LOGW("radio", "channel %u refused (0x%x)",
                static_cast<unsigned>(channel), static_cast<unsigned>(ch));
        esp_wifi_set_promiscuous(false);
        return false;
    }

    _hunting.store(true, std::memory_order_release);
    return true;
}

void RadioService::huntBle(const uint8_t* mac) {
    if (!_ble || !mac) return;
    _hunting.store(false, std::memory_order_release);
    std::memcpy(_huntMac, mac, 6);
    _head.store(0, std::memory_order_relaxed);
    _tail.store(0, std::memory_order_relaxed);
    _scanning.store(false, std::memory_order_release);
    _hunting.store(true, std::memory_order_release);
    if (g_scan) g_scan->start(0, nullptr, false);
}

// -----------------------------------------------------------------------------
//  Channel state information
// -----------------------------------------------------------------------------
void radioCsiCb(void* ctx, wifi_csi_info_t* info) {
    (void)ctx;
    // Acquire on the gate: beginCsi() emptied the ring before setting it, and
    // this is what makes that reset visible on this core.
    if (!g_radio || !g_radio->_csi.load(std::memory_order_acquire) || !info ||
        !info->buf || info->len == 0)
        return;

    // One transmitter, or the measurement is meaningless: a metric that mixed
    // several access points would report a change every time a different one
    // was heard, which is a schedule, not a room.
    if (!macEqual(info->mac, g_radio->_huntMac)) return;

    const uint32_t head = g_radio->_csiHead.load(std::memory_order_relaxed);
    const uint32_t next = (head + 1) % RadioService::kCsiQueue;
    if (next == g_radio->_csiTail.load(std::memory_order_acquire)) {
        // Full. Drop the newest and say so rather than block the WiFi task.
        //
        // Loaded and stored rather than fetch_add: this build passes
        // -mdisable-hardware-atomics, so every read-modify-write becomes a call
        // into libatomic, which does it by disabling interrupts. A global
        // critical section is not something to take in the WiFi driver's
        // callback to count a dropped frame, and only this task writes the
        // counter, so it does not need one.
        g_radio->_csiDropped.store(
            g_radio->_csiDropped.load(std::memory_order_relaxed) + 1,
            std::memory_order_relaxed);
        return;
    }

    CsiFrame& f = g_radio->_csiQueue[head];
    uint16_t  n = info->len;
    if (n > CsiFrame::kMaxBytes) n = CsiFrame::kMaxBytes;
    std::memcpy(f.data, info->buf, n);
    f.len              = n;
    f.firstWordInvalid = info->first_word_invalid;
    f.rssi             = static_cast<int8_t>(info->rx_ctrl.rssi);
    f.atMs             = millis();

    // Release last, and this is the one that mattered: 384 bytes of memcpy have
    // to land before the index that hands them over.
    g_radio->_csiHead.store(next, std::memory_order_release);
}

/// Puts the radio back the way CSI found it. Shared by the failure exit in
/// beginCsi() and by endCsi(), so a measurement that never started is torn down
/// exactly like one that did.
static void csiTeardown() {
    esp_wifi_set_csi(false);
    esp_wifi_set_csi_rx_cb(nullptr, nullptr);
    esp_wifi_set_promiscuous(false);
}

bool RadioService::beginCsi(const uint8_t* bssid, uint8_t channel) {
    if (!_wifi || !bssid) return false;
    std::memcpy(_huntMac, bssid, 6);
    _csiHead.store(0, std::memory_order_relaxed);
    _csiTail.store(0, std::memory_order_relaxed);
    _csiDropped.store(0, std::memory_order_relaxed);

    // Every failure past this point has the sniffer running with `_csi` still
    // false, so endCsi() would decline to take it down and the radio would stay
    // in promiscuous mode on somebody else's channel, feeding a callback nobody
    // reads. One exit, so the next failure added here cannot forget to undo it.
    const auto giveUp = [] { csiTeardown(); return false; };

    // Sniffer first: CSI is computed for packets the radio actually receives,
    // and a disconnected station receives none.
    esp_wifi_set_promiscuous(false);
    wifi_promiscuous_filter_t filter{};
    filter.filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT | WIFI_PROMIS_FILTER_MASK_DATA;
    esp_wifi_set_promiscuous_filter(&filter);
    esp_wifi_set_promiscuous_rx_cb(&radioPromiscuousCb);
    if (esp_wifi_set_promiscuous(true) != ESP_OK) {
        SD_LOGW("radio", "promiscuous mode refused");
        return giveUp();
    }
    // A channel the country setting does not allow is refused here and the
    // radio stays where it was, so CSI would run on the wrong channel and the
    // frames would never arrive.
    const esp_err_t ch = esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
    if (ch != ESP_OK) {
        SD_LOGW("radio", "channel %u refused (0x%x)",
                static_cast<unsigned>(channel), static_cast<unsigned>(ch));
        return giveUp();
    }

    wifi_csi_config_t cfg{};
    cfg.lltf_en           = true;
    cfg.htltf_en          = true;
    cfg.stbc_htltf2_en    = true;
    cfg.ltf_merge_en      = true;
    // The one deliberate deviation from the defaults. Channel filtering smooths
    // each sub-carrier against its neighbours, which is what you want when CSI
    // is equalising a packet and exactly what you do not want here: it blurs
    // away the fine structure across frequency that a person moving creates.
    cfg.channel_filter_en = false;
    cfg.manu_scale        = false;  // let the hardware scale; the metric is
    cfg.shift             = 0;      // normalised, so its choice cannot matter

    if (esp_wifi_set_csi_config(&cfg) != ESP_OK) {
        SD_LOGW("radio", "csi config rejected");
        return giveUp();
    }
    if (esp_wifi_set_csi_rx_cb(&radioCsiCb, nullptr) != ESP_OK) {
        SD_LOGW("radio", "csi callback refused");
        return giveUp();
    }
    if (esp_wifi_set_csi(true) != ESP_OK) {
        SD_LOGW("radio", "csi not supported by this build");
        return giveUp();
    }

    _csi.store(true, std::memory_order_release);
    SD_LOGI("radio", "csi up on ch%u, bssid %02X:%02X:%02X:%02X:%02X:%02X",
            static_cast<unsigned>(channel), bssid[0], bssid[1], bssid[2],
            bssid[3], bssid[4], bssid[5]);
    return true;
}

void RadioService::endCsi() {
    if (!_csi.load(std::memory_order_relaxed)) return;
    _csi.store(false, std::memory_order_release);
    csiTeardown();
}

bool RadioService::popCsi(CsiFrame& out) {
    const uint32_t tail = _csiTail.load(std::memory_order_relaxed);
    if (tail == _csiHead.load(std::memory_order_acquire)) return false;
    out = _csiQueue[tail];
    // Release after the copy out, or the producer could refill the slot while
    // this frame is still being read from it.
    _csiTail.store((tail + 1) % kCsiQueue, std::memory_order_release);
    return true;
}

void RadioService::huntStop() {
    _hunting.store(false, std::memory_order_release);
    if (_wifi) esp_wifi_set_promiscuous(false);
}

}  // namespace sd
