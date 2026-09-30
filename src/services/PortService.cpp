#include "services/PortService.h"

#include "core/Log.h"

#include <Arduino.h>
#include <M5Unified.h>

namespace sd {
namespace {

struct Known {
    uint8_t     addr;
    const char* names;
};

// Addresses seen often enough on Grove-style modules to be worth naming. Every
// entry is a list because I2C addresses are not unique: 0x68 is an IMU on one
// module and a real-time clock on the next. The screen offers suspects and
// leaves the identification to whoever is holding the board.
//
// Entries earn their place by being seen, not by being remembered. An ENV III
// on the bench answered on 0x44 and 0x70 and has exactly two chips, which
// proved the QMP6988 sits on 0x70 and corrected two guesses at once: it was
// missing from 0x70 and wrongly listed against 0x76, where the BMP280 family
// lives. A wrong name costs more than no name, because it stops the search.
constexpr Known kKnown[] = {
    {0x0D, "QMC5883L compass"},
    {0x18, "ES8311 codec / LIS2DH"},
    {0x1E, "HMC5883L / LSM303"},
    {0x23, "BH1750 light"},
    {0x29, "VL53L0X ToF / TCS34725"},
    {0x38, "AHT20 / FT6336 touch"},
    {0x39, "APDS-9960 gesture"},
    {0x3C, "SSD1306 OLED"},
    {0x40, "INA219 / SHT21"},
    {0x44, "SHT30 / SHT31 / SHT4x"},
    {0x48, "ADS1115 ADC"},
    {0x50, "24Cxx EEPROM"},
    {0x51, "BM8563 RTC"},
    {0x53, "ADXL345"},
    {0x57, "Ultrasonic / MAX30102"},
    {0x5A, "MLX90614 NCIR / CCS811"},
    {0x60, "MCP4725 DAC / ATECC608"},
    {0x61, "SCD30 CO2"},
    {0x62, "SCD4x CO2"},
    {0x68, "BMI270 / MPU6886 / DS3231"},
    {0x69, "BMI270 alt / MPU6886 alt"},
    {0x6E, "M5PM1 PMIC"},
    {0x70, "QMP6988 baro / TCA9548A"},
    {0x76, "BMP280 / BME280"},
    {0x77, "BMP280 alt / BME280 alt"},
};

}  // namespace

// -----------------------------------------------------------------------------
m5::I2C_Class& PortService::i2c() const {
    return _bus == Bus::PortA ? M5.Ex_I2C : M5.In_I2C;
}

bool PortService::begin() {
    if (_on) return true;
    // The internal bus is already running, M5.begin() needing it for the IMU
    // and the PMIC, so opening it again would be at best redundant and at worst
    // a reconfiguration underneath a driver that is mid-conversation.
    _on = (_bus == Bus::Internal) ? M5.In_I2C.isEnabled() : M5.Ex_I2C.begin();
    if (!_on) SD_LOGW("port", "external I2C would not start");
    return _on;
}

void PortService::end() {
    if (_on && _bus == Bus::PortA) M5.Ex_I2C.release();
    _on = false;
}

void PortService::selectBus(Bus b) {
    if (b == _bus) return;
    end();
    _bus = b;
    begin();
}

// -----------------------------------------------------------------------------
//  Wiring
// -----------------------------------------------------------------------------
PortService::Line PortService::sampleLine(int pin) const {
    // Two questions, one per pull direction. A line still high against the
    // internal pull-down is being held up by something far stronger, the 4k7 to
    // 10k pull-up every I2C module carries, which is the presence test. A line
    // still low against the internal pull-up is being held down, which no idle
    // bus ever is.
    pinMode(pin, INPUT_PULLDOWN);
    delayMicroseconds(300);
    const bool high = digitalRead(pin) != 0;

    pinMode(pin, INPUT_PULLUP);
    delayMicroseconds(300);
    const bool low = digitalRead(pin) == 0;

    pinMode(pin, INPUT);
    if (low)  return Line::Held;
    return high ? Line::Idle : Line::Floating;
}

PortService::Wiring PortService::checkWiring() {
    Wiring w;
    if (_bus != Bus::PortA) return w;  // never take the IMU's bus apart

    const bool was = _on;
    if (was) M5.Ex_I2C.release();

    w.scl = sampleLine(kSclPin);
    w.sda = sampleLine(kSdaPin);

    if (was) M5.Ex_I2C.begin();
    return w;
}

bool PortService::recover() {
    if (_bus != Bus::PortA) return false;

    const bool was = _on;
    if (was) M5.Ex_I2C.release();

    pinMode(kSdaPin, INPUT_PULLUP);
    pinMode(kSclPin, OUTPUT_OPEN_DRAIN);
    digitalWrite(kSclPin, HIGH);
    delayMicroseconds(10);

    // A device that hung part-way through returning a byte is waiting for the
    // clocks it never got. Give it up to nine and it finishes the byte, sees
    // the NACK it expects, and lets go.
    for (int i = 0; i < 9 && digitalRead(kSdaPin) == 0; ++i) {
        digitalWrite(kSclPin, LOW);
        delayMicroseconds(10);
        digitalWrite(kSclPin, HIGH);
        delayMicroseconds(10);
    }

    // Then a stop condition, so the next transfer starts from a clean bus.
    pinMode(kSdaPin, OUTPUT_OPEN_DRAIN);
    digitalWrite(kSdaPin, LOW);
    delayMicroseconds(10);
    digitalWrite(kSdaPin, HIGH);
    delayMicroseconds(10);

    pinMode(kSdaPin, INPUT_PULLUP);
    delayMicroseconds(300);
    const bool freed = digitalRead(kSdaPin) != 0;
    pinMode(kSdaPin, INPUT);
    pinMode(kSclPin, INPUT);

    if (was) M5.Ex_I2C.begin();
    SD_LOGI("port", "bus recovery %s", freed ? "freed the line" : "did not help");
    return freed;
}

// -----------------------------------------------------------------------------
//  Traffic
// -----------------------------------------------------------------------------
bool PortService::probe(uint8_t addr) {
    if (!_on) return false;
    return i2c().scanID(addr, _hz);
}

bool PortService::readReg(uint8_t addr, uint8_t reg, uint8_t& out) {
    if (!_on) return false;
    return i2c().readRegister(addr, reg, &out, 1, _hz);
}

bool PortService::readRaw(uint8_t addr, uint8_t* buf, size_t len) {
    if (!_on || len == 0) return false;
    auto& bus = i2c();
    if (!bus.start(addr, true, _hz)) { bus.stop(); return false; }
    const bool ok = bus.read(buf, len, true);
    bus.stop();
    return ok;
}

bool PortService::writeReg(uint8_t addr, uint8_t reg, uint8_t val) {
    if (!_on) return false;
    return i2c().writeRegister8(addr, reg, val, _hz);
}

bool PortService::command(uint8_t addr, const uint8_t* bytes, size_t len) {
    if (!_on || len == 0) return false;
    auto& bus = i2c();
    if (!bus.start(addr, false, _hz)) { bus.stop(); return false; }
    const bool ok = bus.write(bytes, len);
    return bus.stop() && ok;
}

const char* PortService::candidates(uint8_t addr) {
    for (const Known& k : kKnown) {
        if (k.addr == addr) return k.names;
    }
    return nullptr;
}

}  // namespace sd
