#include "services/IrService.h"

#include "core/Log.h"

#include <driver/gpio.h>

namespace sd {

// -----------------------------------------------------------------------------
bool IrService::begin() {
    if (_ch) return true;

    rmt_rx_channel_config_t cfg{};
    cfg.gpio_num          = static_cast<gpio_num_t>(kRxPin);
    cfg.clk_src           = RMT_CLK_SRC_DEFAULT;
    cfg.resolution_hz     = 1000000;  // one tick per microsecond
    cfg.mem_block_symbols = 128;      // rounded up to whole 48-symbol blocks
    // Polarity is deliberately left alone. Which level means "carrier present"
    // is worked out from the captured data in flatten(), so nothing here has to
    // be right about how the receiver is wired or about the driver's input
    // inverter in the next IDF.

    if (rmt_new_rx_channel(&cfg, &_ch) != ESP_OK) {
        _ch = nullptr;
        SD_LOGE("ir", "rmt rx channel failed");
        return false;
    }

    rmt_rx_event_callbacks_t cbs{};
    cbs.on_recv_done = &IrService::onDone;
    if (rmt_rx_register_event_callbacks(_ch, &cbs, this) != ESP_OK ||
        rmt_enable(_ch) != ESP_OK) {
        rmt_del_channel(_ch);
        _ch = nullptr;
        SD_LOGE("ir", "rmt rx enable failed");
        return false;
    }

    // RMT's own glitch filter is 8 bits of APB ticks, about 3.2 us at the
    // ceiling, so it can only reject electrical glitches, never the pulses an
    // IR demodulator invents while staring at a lamp. Ask for more and
    // rmt_receive() rejects the whole call. Optical noise is dealt with in
    // buildTrain() and in the app's capture gate.
    _rxCfg.signal_range_min_ns = 1000;
    // 12 ms of quiet ends the frame. The widest gap inside any protocol here is
    // NEC's 4.5 ms header, so this keeps 2.7x margin, and the narrowest gap
    // BETWEEN frames is Sony's 21 ms. Shortening it from the vendor's 20 ms
    // cuts the window in which the receiver sits unarmed, and every millisecond
    // of that is a chance to start recording half way through a frame.
    _rxCfg.signal_range_max_ns = 12000000;

    _wr = 0;
    _rd = 0;
    _receiving = false;
    _faulted   = false;
    _armFails  = 0;
    _captured = 0;
    _dropped  = 0;
    arm();
    SD_LOGI("ir", "rx up on G%d", kRxPin);
    return true;
}

void IrService::end() {
    if (!_ch) return;
    rmt_disable(_ch);
    rmt_del_channel(_ch);
    _ch        = nullptr;
    _receiving = false;
    SD_LOGI("ir", "rx down");
}

// -----------------------------------------------------------------------------
//  The interrupt does the minimum only it can do: note how many symbols landed
//  and hand the slot over. It could legally call rmt_receive() itself, but that
//  is flash-resident code, and calling into flash from an interrupt is a crash
//  waiting for some other task to write to NVS. Re-arming from the tick costs
//  at most one loop period, and IR frames are never closer than about 40 ms.
// -----------------------------------------------------------------------------
bool IrService::onDone(rmt_channel_handle_t, const rmt_rx_done_event_data_t* edata,
                       void* ctx) {
    auto* self = static_cast<IrService*>(ctx);

    const uint8_t next = static_cast<uint8_t>((self->_wr + 1) % kSlots);
    if (next == self->_rd) {
        // The app has not kept up. Drop this frame by leaving the write index
        // where it is, so the same slot is reused for the next capture.
        self->_dropped = self->_dropped + 1;
    } else {
        size_t n = edata->num_symbols;
        if (n > kSymbols) n = kSymbols;
        self->_len[self->_wr] = static_cast<uint16_t>(n);
        self->_wr             = next;
        self->_captured = self->_captured + 1;
    }
    self->_receiving = false;
    return false;
}

void IrService::arm() {
    if (!_ch || _receiving || _faulted) return;

    const esp_err_t err = rmt_receive(_ch, _slot[_wr], sizeof(_slot[_wr]), &_rxCfg);
    if (err == ESP_OK) {
        _receiving = true;
        _armFails  = 0;
        return;
    }
    // A rejected configuration will be rejected every time, and re-arming from
    // the tick turns that into a hundred identical log lines a second with no
    // hint on screen. Give up after a few, once, loudly.
    if (++_armFails >= 3) {
        _faulted = true;
        SD_LOGE("ir", "rx arm rejected (%s) - receiver stopped", esp_err_to_name(err));
    }
}

void IrService::pump() { arm(); }

// -----------------------------------------------------------------------------
bool IrService::poll(PulseTrain& out) {
    if (_rd == _wr) return false;
    const uint8_t slot = _rd;
    flatten(_slot[slot], _len[slot], out);
    _rd = static_cast<uint8_t>((slot + 1) % kSlots);
    return true;
}

// -----------------------------------------------------------------------------
//  Transmit
// -----------------------------------------------------------------------------
bool IrService::beginTx() {
    if (_txCh) return true;

    rmt_tx_channel_config_t cfg{};
    cfg.gpio_num          = static_cast<gpio_num_t>(kTxPin);
    cfg.clk_src           = RMT_CLK_SRC_DEFAULT;
    cfg.resolution_hz     = 1000000;  // same microsecond grid the capture uses
    cfg.mem_block_symbols = 64;
    cfg.trans_queue_depth = 2;

    if (rmt_new_tx_channel(&cfg, &_txCh) != ESP_OK) {
        _txCh = nullptr;
        SD_LOGE("ir", "rmt tx channel failed");
        return false;
    }

    rmt_tx_event_callbacks_t txcbs{};
    txcbs.on_trans_done = &IrService::onTxDone;
    rmt_tx_register_event_callbacks(_txCh, &txcbs, this);

    // Carrier first, then the encoder, then enable: the order M5Stack's own
    // example uses. The registers are writable either way, but a working vendor
    // example beats a reading of the driver source.
    rmt_carrier_config_t car{};
    car.frequency_hz = 38000;
    car.duty_cycle   = 0.33f;  // M5Stack's own figure
    rmt_apply_carrier(_txCh, &car);
    _carrierHz = 38000;

    rmt_copy_encoder_config_t enc{};
    if (rmt_new_copy_encoder(&enc, &_txEnc) != ESP_OK || rmt_enable(_txCh) != ESP_OK) {
        if (_txEnc) { rmt_del_encoder(_txEnc); _txEnc = nullptr; }
        rmt_del_channel(_txCh);
        _txCh = nullptr;
        SD_LOGE("ir", "rmt tx enable failed");
        return false;
    }

    _txBusy    = false;
    _sent      = 0;
    SD_LOGI("ir", "tx up on G%d", kTxPin);
    return true;
}

void IrService::endTx() {
    if (!_txCh) return;
    rmt_disable(_txCh);
    rmt_del_encoder(_txEnc);
    rmt_del_channel(_txCh);
    _txEnc   = nullptr;
    _txCh    = nullptr;
    _txBusy  = false;
    // Hand the pin back as a floating input, the state it booted in and the
    // state it was in when reception demonstrably worked. Driving it to a level
    // means knowing which way round the emitter is wired, and guessing wrong
    // lights an IR LED a centimetre from the receiver, which looks like ambient
    // noise and is not.
    gpio_config_t io{};
    io.pin_bit_mask = 1ULL << kTxPin;
    io.mode         = GPIO_MODE_INPUT;
    io.pull_up_en   = GPIO_PULLUP_DISABLE;
    io.pull_down_en = GPIO_PULLDOWN_DISABLE;
    gpio_config(&io);
    SD_LOGI("ir", "tx down");
}

/// The transmit interrupt does one thing: clear the flag. No calls, no
/// allocation, nothing that could be in flash.
bool IrService::onTxDone(rmt_channel_handle_t, const rmt_tx_done_event_data_t*,
                         void* ctx) {
    static_cast<IrService*>(ctx)->_txBusy = false;
    return false;
}

bool IrService::sending() {
    if (!_txBusy) return false;
    // A deadline as well as the callback. rmt_tx_wait_all_done() cannot be used
    // to poll, a zero timeout being a failed wait that logs one error line per
    // call, so a missed completion would otherwise leave the transmitter busy
    // forever.
    if (static_cast<int32_t>(millis() - _txDeadline) > 0) _txBusy = false;
    return _txBusy;
}

bool IrService::send(const PulseTrain& t, uint16_t carrierHz) {
    if (!_txCh || sending() || t.count == 0) return false;

    // Applied every time rather than only on a change. It is a handful of
    // register writes against a frame tens of milliseconds long, and it removes
    // any question of whether the setting is still in force: a transmission
    // with no carrier is invisible to every receiver in the world while looking
    // perfectly healthy from this side.
    rmt_carrier_config_t car{};
    car.frequency_hz = carrierHz ? carrierHz : 38000;
    car.duty_cycle   = 0.33f;  // M5Stack's own figure
    if (rmt_apply_carrier(_txCh, &car) != ESP_OK) return false;
    _carrierHz = car.frequency_hz;

    size_t n = 0;
    for (int i = 0; i < t.count && n < kTxSymbols;) {
        const uint32_t mark  = t.us[i++];
        const uint32_t space = (i < t.count) ? t.us[i++] : kTxTailUs;
        _txSym[n].duration0 = mark & 0x7FFF;
        _txSym[n].level0    = 1;  // carrier rides on the high level
        _txSym[n].duration1 = space & 0x7FFF;
        _txSym[n].level1    = 0;
        ++n;
    }
    _txCount = n;

    rmt_transmit_config_t tc{};
    tc.loop_count           = 0;
    tc.flags.queue_nonblocking = 1;
    if (rmt_transmit(_txCh, _txEnc, _txSym, n * sizeof(rmt_symbol_word_t), &tc) != ESP_OK)
        return false;

    // Frame length plus enough slack for the queue and the callback.
    _txDeadline = millis() + (t.totalUs() / 1000) + 50;
    _txBusy     = true;
    ++_sent;
    return true;
}

// -----------------------------------------------------------------------------
/// Turns RMT's packed level/duration pairs into a plain alternating train. All
/// the judgement (signal polarity, leading fragments, trailing silence) lives
/// in buildTrain(), where it can be tested on the host.
void IrService::flatten(const rmt_symbol_word_t* sym, size_t n, PulseTrain& out) {
    buildTrain(
        static_cast<int>(n) * 2, n >= kSymbols, kMinPulseUs,
        [sym](int i) {
            const rmt_symbol_word_t& s = sym[i >> 1];
            return (i & 1) ? PulseEdge{s.duration1, static_cast<uint8_t>(s.level1)}
                           : PulseEdge{s.duration0, static_cast<uint8_t>(s.level0)};
        },
        out);
}

}  // namespace sd
