#pragma once
// -----------------------------------------------------------------------------
//  PortService - PORT.A, the HY2.0-4P connector.
//
//  Pinout is black GND, red 5V, yellow G10, white G9. M5Unified already knows
//  this: its board table maps the StickS3's external I2C bus to those two pins,
//  so the bus is M5.Ex_I2C and the pin numbers below are needed only for the
//  wiring check, which drives the lines by hand.
//
//  The 5V pin is the EXT_5V rail, which M5Unified switches off at boot, so
//  Cap::PortA implies Cap::Ext5V: an unpowered module cannot answer and cannot
//  hold the bus up, and "no devices found" would be a lie about the wiring.
//
//  Nothing here interprets what it finds. "Is anything out there, and is the
//  wiring sound?" is a different job from knowing what a sensor means, and only
//  the first belongs to every future PORT.A app.
// -----------------------------------------------------------------------------
#include <cstddef>
#include <cstdint>

namespace m5 { class I2C_Class; }

namespace sd {

class PortService {
public:
    /// PORT.A signal pins. Yellow is the clock, white the data.
    static constexpr int kSclPin = 10;
    static constexpr int kSdaPin = 9;

    /// M5Unified's scanner starts at 8, with a comment that addresses 0-7 hang
    /// the ESP32-S3 outright. Reserved addresses either way, so no loss.
    static constexpr uint8_t kFirstAddr = 0x08;
    static constexpr uint8_t kLastAddr  = 0x77;

    /// The internal bus carries the IMU and the PMIC. Scanning it proves the
    /// tool works when the external port is empty, which is otherwise
    /// indistinguishable from the tool being broken.
    enum class Bus : uint8_t { PortA, Internal };

    bool begin();
    void end();
    bool ready() const { return _on; }

    void selectBus(Bus b);
    Bus  bus() const { return _bus; }
    bool external() const { return _bus == Bus::PortA; }

    uint32_t clockHz() const { return _hz; }
    void     setClockHz(uint32_t hz) { _hz = hz; }

    // -- wiring ---------------------------------------------------------------
    /// What one line does when nobody is driving it.
    ///   Idle     - held high by an external pull-up: a module is present
    ///   Floating - nothing holding it either way: an empty port
    ///   Held     - stuck low: a short, or a device that hung mid-transfer
    enum class Line : uint8_t { Idle, Floating, Held, Unknown };
    struct Wiring { Line sda = Line::Unknown; Line scl = Line::Unknown; };

    /// Only meaningful on PORT.A; the internal bus is never taken apart to be
    /// measured, the IMU and the PMIC living on it.
    Wiring checkWiring();

    /// Nine clock pulses and a stop, the standard way to walk a device off a
    /// bus it is holding low. Returns true if SDA came back up.
    bool recover();

    // -- traffic --------------------------------------------------------------
    bool probe(uint8_t addr);
    bool readReg(uint8_t addr, uint8_t reg, uint8_t& out);
    /// Read with no register phase, for devices that have no register map.
    bool readRaw(uint8_t addr, uint8_t* buf, size_t len);

    // The two writes below exist for sensor drivers, which have identified the
    // part they are talking to and know what its registers mean. Nothing that
    // explores an unknown bus should call them: writing to a register you
    // cannot name is how a module ends up with a new I2C address that outlives
    // the power cycle.
    bool writeReg(uint8_t addr, uint8_t reg, uint8_t val);
    /// Send bytes with no register phase, for parts that take commands rather
    /// than register writes: every Sensirion device, among others.
    bool command(uint8_t addr, const uint8_t* bytes, size_t len);

    /// Devices commonly found at an address, or nullptr. A hint to recognise
    /// what you plugged in, never a claim: addresses collide freely, and the
    /// only proof is the device answering the way its datasheet says.
    static const char* candidates(uint8_t addr);

private:
    m5::I2C_Class& i2c() const;
    Line           sampleLine(int pin) const;

    Bus      _bus = Bus::PortA;
    uint32_t _hz  = 100000;
    bool     _on  = false;
};

}  // namespace sd
