#pragma once
// -----------------------------------------------------------------------------
//  IrService - the infrared receiver, via the RMT peripheral.
//
//  Bit-banging a GPIO cannot capture IR on this board; M5Stack say so outright
//  and they are right, because a 560 us mark measured from a cooperatively
//  scheduled loop is a 560 us mark measured badly. RMT timestamps the edges in
//  hardware, and the CPU only ever sees a finished frame.
//
//  The vendor's own NEC example sets the configuration used below: 1 us
//  resolution, 128 symbol blocks, a 1 us glitch filter and a 20 ms idle
//  timeout.
//
//  Two board rules are handled in ServiceHub and are worth knowing here: the
//  receiver is unpowered until EXT_5V is switched on, and it hears nothing at
//  all while the speaker amplifier is running.
//
//  Signal polarity is not configured anywhere. It is recovered from each
//  capture, so nothing here depends on how the receiver happens to be wired.
// -----------------------------------------------------------------------------
#include "mathx/IrDecode.h"

#include <driver/rmt_encoder.h>
#include <driver/rmt_rx.h>
#include <driver/rmt_tx.h>

#include <cstdint>

namespace sd {

class IrService {
public:
    static constexpr int kRxPin = 42;  // ESP32-S3 G42
    static constexpr int kTxPin = 46;  // ESP32-S3 G46, unused for now

    /// Shortest run treated as real. Denon's 264 us mark is the tightest any
    /// protocol sends, so 100 us keeps a comfortable margin while folding away
    /// the spikes a demodulator produces on its own.
    static constexpr uint16_t kMinPulseUs = 100;

    bool begin();
    void end();
    bool running() const { return _ch != nullptr && !_faulted; }
    /// The receiver was created but the driver refused to arm it.
    bool faulted() const { return _faulted; }

    /// Re-arms the receiver after a completed capture. Call once per tick; the
    /// interrupt deliberately does not re-arm itself (see the .cpp).
    void pump();

    /// Takes the oldest waiting frame. False when there is nothing to read.
    bool poll(PulseTrain& out);

    uint32_t captured() const { return _captured; }
    uint32_t dropped() const  { return _dropped; }

    // -- transmit -------------------------------------------------------------
    //  A copy encoder is all this needs: a captured train already IS the symbol
    //  stream, so replay works for a protocol nobody has decoded just as well
    //  as for one that has a name.
    bool beginTx();
    void endTx();
    bool txReady() const { return _txCh != nullptr; }

    /// Queues `t` for transmission at `carrierHz`. False if the transmitter is
    /// busy or unavailable. Non-blocking.
    bool send(const PulseTrain& t, uint16_t carrierHz);

    /// True while a frame is still going out. Polls the driver, so call it from
    /// the tick rather than testing a stale flag.
    bool sending();

    uint32_t sent() const { return _sent; }

private:
    /// Three buffers is enough for a strict producer/consumer hand-off: the
    /// hardware fills one while the app reads another and a third is spare.
    static constexpr int kSlots   = 3;
    /// 512 symbols is 1024 pulses, a whole air-conditioner frame rather than
    /// the front of one. It has to match what a PulseTrain can hold, or the
    /// extra capture room is thrown away on the way out.
    static constexpr int kSymbols = PulseTrain::kMax / 2;

    static bool onDone(rmt_channel_handle_t chan,
                       const rmt_rx_done_event_data_t* edata, void* ctx);
    static bool onTxDone(rmt_channel_handle_t chan,
                         const rmt_tx_done_event_data_t* edata, void* ctx);
    static void flatten(const rmt_symbol_word_t* sym, size_t n, PulseTrain& out);

    void arm();

    /// Room for the longest train the receiver can hold, since anything it
    /// captured must be re-sendable.
    static constexpr int kTxSymbols = PulseTrain::kMax / 2 + 1;
    /// The gap appended after the closing burst. A train ends on a mark, and a
    /// zero-length entry would be read as end-of-transmission by the hardware.
    static constexpr uint16_t kTxTailUs = 1000;

    rmt_channel_handle_t _ch = nullptr;
    rmt_receive_config_t _rxCfg{};

    rmt_channel_handle_t _txCh  = nullptr;
    rmt_encoder_handle_t _txEnc = nullptr;
    rmt_symbol_word_t    _txSym[kTxSymbols]{};
    size_t               _txCount   = 0;
    uint16_t             _carrierHz = 0;
    volatile bool        _txBusy    = false;
    uint32_t             _txDeadline = 0;
    uint32_t             _sent      = 0;

    rmt_symbol_word_t _slot[kSlots][kSymbols]{};
    volatile uint16_t _len[kSlots]{};
    volatile uint8_t  _wr = 0;   ///< advanced by the interrupt
    volatile uint8_t  _rd = 0;   ///< advanced by the app
    volatile bool     _receiving = false;
    bool              _faulted   = false;
    uint8_t           _armFails  = 0;

    volatile uint32_t _captured = 0;
    volatile uint32_t _dropped  = 0;
};

}  // namespace sd
