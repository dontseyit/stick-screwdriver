# Device

## Board

SoC,ESP32-S3-PICO-1-N8R8 @ Xtensa® 32-bit LX7 dual-core processor, clock frequency 240MHz
Flash,8MB
PSRAM,8MB Octal
Audio Codec,ES8311: 24-bit resolution, I2S protocol
MEMS microphone, Signal-to-Noise Ratio (SNR): 65 dB
AW8737 power amplifier + 8Ω@1W 2011 cavity speaker
Battery Capacity,250mAh
IMU,BMI270
Integrated IR transmitter + IR receiver
Expansion interfaces:
    Expandable Hat2 bus (2.54-16P)
    HY2.0-4P interface

## Infrared Reception Notes
1.The infrared receiving and decoding of StickS3 must use the ESP32 RMT peripheral and does not support receiving and decoding via GPIO method.
2. When using the infrared receiver function, the speaker amplifier must be turned off; otherwise, reception will not work properly. The operation method can be referenced in the tutorial
3. Ensure that the transmitter and receiver are aligned as directly as possible, and keep a distance of no less than 30 cm between them. If the distance is too short, abnormal reception may occur.

## Speaker Volume Notice
When powered by battery (USB not connected), it is recommended to keep the speaker volume below 75% to avoid unexpected device reboot caused by excessive power consumption.

## EXT_5V_EN

In the default initialization of M5Unified, EXT_5V_EN is disabled. This operation turns off the power supply to the Grove, Hat EXT_5V interfaces, and IR TX/RX, and switches them to input mode. In this state, an external 5V power input is required for IR TX/RX to operate properly. For use cases without an external power supply, you can re-enable the EXT_5V output mode through the following API to restore power to IR TX/RX.

```cpp
M5.Power.setExtOutput(true);   // EXT_5V OUTPUT
// M5.Power.setExtOutput(false);  // EXT_5V INPUT
```

## PinMap

### LCD
ESP32‑S3,G39,G40,G45,G41,G21,G38
ST7789P3,MOSI,SCK,RS,CS,RST,BL

### IMU & M5PM1
ESP32‑S3,G48,G47
BMI270 (0x68),SCL,SDA
M5PM1 (0x6e),SCL,SDA

### M5PM1
M5PM1,G0,G1,G2,G3,G4
Battery Charge,PYG0_CHG_STAT	
ESP32-S3,,G13
L3B Power,,,PYG2_L3B_EN
Speaker,,,,PYG3_SPK_Pulse
IMU INT,,,,,PYG4_IMU_INT

### Audio
ESP32‑S3,G18,G14,G17,G15,G16,G48,G47
ES8311 (0x18),MCLK,DOUT,BCLK,LRCK,DIN,SCL,SDA

### Button
ESP32‑S3,G11,G12
Input,KEY1,KEY2

### IR
ESP32‑S3,G46,G42
IR,IR_TX,IR_RX

### Hat2 bus (2.54-16P)
Exposed GPIO,G0,G1,G2,G3,G4,G5,G6,G7,G8,G43,G44
Also on header,EXT_5V,5VIN,3V3,GND

G1-G8 are ADC1 channels 0-7. ADC1 matters: ADC2 shares hardware with the radio
and cannot be read while WiFi is up.

Do not use for input:
  G0   Boot button, strapping pin
  G3   strapping pin - selects the JTAG source at reset
  G43  serial console TX (U0TXD)
  G44  serial console RX (U0RXD)

### HY2.0-4P
PORT.A
HY2.0-4P,Black,Red,Yellow,White
PORT.CUSTOM,GND,5V,G9,G10

## Power management

Collected 2026-09-11 from docs.m5stack.com/en/arduino/m5sticks3/m5pm1 and /battery, the M5PM1 Arduino library 1.0.6 (github.com/m5stack/M5PM1, README_FUNCTION_EN.md), the M5PM1 Chip User Manual v1.9 (m5stack-doc.oss-cn-shenzhen.aliyuncs.com/1207/M5PM1_Datasheet_EN.pdf) and M5Unified 0.2.21 `utility/power/M5PM1_Class.*`. Where the sources disagree, the manual wins.

### In this project

- The PMIC sits on I2C SDA 47 / SCL 48 at 0x6E, 100 kHz (400 kHz switchable). M5GFX owns that bus as `I2C_NUM_1` and already talks to the PMIC at init (probe, L3B on, `I2C_CFG = 0`). Talk to it through `m5gfx::i2c::*` on `I2C_NUM_1` or M5Unified's `M5PM1_Class` over `In_I2C`. Do not call `Wire`/`Wire1.begin(47, 48)` or the M5PM1 library's `begin(TwoWire*, ...)` / `begin(i2c_port_t, ...)`: a second driver on the same pins breaks both. The M5PM1 library's `begin(m5::I2C_Class*, addr, speed)` overload is the compatible one; include M5Unified before M5PM1.
- Firmware that never touches the PMIC is safe: watchdog off by default, no power-hold pin, L0/L1/L2 on at PMIC start, L3B switched on by M5GFX.
- Battery mode limits: speaker volume under 75 % (see Speaker Volume Notice), and USB/5VOUT readings under 4 V count as absent.

### Power tree

Not cascaded: L1 to L3B are each fed from L0, so they switch independently.

| Level | Powers | Switched by | Default |
| --- | --- | --- | --- |
| L0 | the PMIC itself, from the battery | always on while the battery has charge | on |
| L1 | IMU (LDO 3V3) | `PWR_CFG` bit 2, `setLdoEnable()`; survives shutdown with `HOLD_CFG` bit 5 | on at PMIC start |
| L2 | ESP32-S3 in sleep (DCDC 3V3) | `PWR_CFG` bit 1 | on at PMIC start |
| L3A | ESP32-S3 active, Grove, Hat, IR transceiver, key pull-ups; EXT_5V through the boost | `PWR_CFG` bit 3 "5VIN/OUT" (`M5.Power.setExtOutput()`); survives shutdown with `HOLD_CFG` bit 6 | 5 V interface in INPUT mode; M5Unified leaves it off, so IR TX/RX need `setExtOutput(true)` |
| L3B | LCD and backlight, microphone, speaker | PMIC GPIO2 (PYG2) high | off until M5GFX or M5Unified init |
| speaker amp | AW8737 | PMIC GPIO3 (PYG3) high = on | M5Unified drives it low at begin; must be off while receiving IR |

Verbatim warning: "When configured as output mode, power input is only allowed via USB or the top Hat2-Bus 5VIN. Do not supply power from other output interfaces; otherwise, there is a risk of short circuit damage to the device."

### Power button

The side button is `PWR_BTN` on the PMIC, not an ESP32 GPIO.

- Single click: system reset through `BOOT_OUT`. Disable with `setSingleResetDisable(true)` ("high risk"); a single click then raises IRQ Status 3 bit 0 instead.
- Double click: power off to L0. Disable with `setDoubleOffDisable(true)` ("high risk"); a double click then raises IRQ Status 3 bit 2.
- Download mode: `enterDownloadMode()` (`SYS_CMD`), lockable with `setDownloadLock(true)`. Entering it clears `HOLD_CFG`, stops the watchdog, I2C sleep and timer, and returns GPIOs and rails to defaults.
- LED (`PWR_CFG` bit 4, `setLedEnLevel()`): one flash on reset; 500 ms blink in download mode; 200 ms blink when button reset is disabled and a GPIO IRQ is enabled; 100 ms blink when double-click off is disabled and a GPIO IRQ is enabled.
- None of the sources read gives press durations for power-on; treat any "hold for N seconds" figure as unverified.

### Battery, charging, power source

- M5Unified: `M5.Power.getBatteryVoltage()` (mV), `getBatteryLevel()` (%), `isCharging()` (StickS3 reads PMIC GPIO0 = `CHG_STAT`, low = charging), `getVBUSVoltage()`. `M5PM1_Class` adds `setBatteryCharge()`, `setChargeCurrent(mA)`, `setChargeVoltage(mV)`, `getPowerSource()`, `get5VoutVoltage()`.
- Registers, 16-bit little-endian millivolts, 1:1 dividers: `VREF` 0x20, `VBAT` 0x22, `VIN` 0x24, `5VOUT` 0x26; raw ADC result 0x28 (12 bit), `ADC_CTRL` 0x2A. `readTemperature()` exists in the M5PM1 library.
- `PWR_SRC` 0x04: bit 0 5VIN valid, bit 1 5VINOUT valid (0 while the boost is on), bit 2 VBAT node valid. "Tracks the node voltage, not physical battery presence."
- Charging is enabled by default and "not affected by reset" (`PWR_CFG` bit 0). `BATT_LVP` 0x08 is the low-voltage protection threshold, default 0x40; `setBatteryLvp()` in the M5PM1 library (units not stated in the extracted text).
- Insertion IRQs: battery insert/remove only fires while charging is disabled; 5VIN/OUT insert/remove only fires in INPUT mode.

### Shutdown, timer, wake

- `shutdown()` writes `SYS_CMD` 0x0C = 0xA1 and drops to L0. Rails and GPIOs whose `HOLD_CFG` (0x07) bit is set keep their state: bit 6 5VIN/OUT, bit 5 LDO, bits 4:0 GPIO4..0 (`ldoSetPowerHold`, `boostSetPowerHold`, `gpioSetPowerHold`). `HOLD_CFG` is cleared by download mode and by every reset.
- Timer: `timerSet(seconds, action)` with `STOP`, `FLAG`, `REBOOT`, `POWERON`, `POWEROFF`; `timerClear()`. POWERON also works from the powered-off state. The timer is cleared by power-off.
- Wake sources are flagged in `WAKE_SRC` 0x05 (clear with `clearWakeSource()`). GPIO wake: `gpioSetWakeEnable(pin, true)`, `gpioSetWakeEdge(pin, edge)`; GPIO0/GPIO2 are mutually exclusive as wake pins, so are GPIO3/GPIO4.
- Motion wake from shutdown: keep the IMU alive, then shut down: `setLdoEnable(true); ldoSetPowerHold(true); setLedEnLevel(true); shutdown();` IMU INT1 on PYG4 wakes the PMIC.
- Motion wake of a deep-sleeping ESP32: PYG4 (IMU INT1 in) to PYG1 as IRQ output to ESP32 GPIO 13. Configure PYG4 input with pull-up, PYG1 output push-pull with `FUNC = IRQ`, unmask only the GPIO4 IRQ, then `esp_sleep_enable_ext0_wakeup(GPIO_NUM_13, 0); rtc_gpio_pullup_en(GPIO_NUM_13); esp_deep_sleep_start();`.
- I2C idle sleep: `I2C_CFG` 0x09 bits 3:0 `SLP_TO`, 0 = off (`setI2cSleepTime()`). The first transaction after sleep fails and wakes the chip; retry. Ineffective while PWM is enabled or in download mode. M5GFX and M5Unified write `I2C_CFG = 0x00`.
- Watchdog: `WDT_CNT` 0x0A counts down in seconds, 0 = disabled (default since firmware 5); feed by writing 0xA5 to `WDT_KEY` 0x0B (`wdtSet`, `wdtFeed`).

### PMIC GPIOs (PYG0..4)

| Pin | Wired to | Alternate functions |
| --- | --- | --- |
| PYG0 | `CHG_STAT` input, low = charging | NeoPixel out; wake (exclusive with PYG2) |
| PYG1 | IRQ output to ESP32 GPIO 13 | ADC1 |
| PYG2 | L3B enable (LCD, mic, speaker) | ADC2; wake (exclusive with PYG0) |
| PYG3 | speaker amp enable, AW8737 pulse | PWM0; wake (exclusive with PYG4) |
| PYG4 | IMU INT1 input | PWM1 |

GPIO registers: `MODE` 0x10, `OUT` 0x11, `IN` 0x12, `DRV` 0x13 (default 0x1F = all open-drain; 0 = push-pull), `PUPD` 0x14/0x15, `FUNC` 0x16/0x17 (00 GPIO, 01 IRQ, 11 special). Outputs only take effect with `FUNC = 00`. PWM frequency is shared by both channels (0x34), duty 0x30, 12 bit.

### Register map

`Device_ID` 0x00 = 0x50, `Device_Model` 0x01 = 0x20, `HW_REV` 0x02, `SW_REV` 0x03, `PWR_SRC` 0x04, `WAKE_SRC` 0x05, `PWR_CFG` 0x06 (default 0x17: LED, LDO, DCDC and charging on, 5 V interface as input), `HOLD_CFG` 0x07, `BATT_LVP` 0x08, `I2C_CFG` 0x09, `WDT_CNT` 0x0A, `WDT_KEY` 0x0B, `SYS_CMD` 0x0C (key 0xA in bits 7:4), GPIO 0x10-0x17, ADC 0x20-0x2A, PWM 0x30-0x35, IRQ status 0x40-0x42 and masks 0x43-0x45, `BTN_Status` 0x48, NeoPixel 0x50 and 0x60-0x9F, RTC RAM 0xA0-0xBF. IRQ mask setters take single items only; use the `*All` variants for ALL/NONE.

## Official Examples and Libraries

- [StickS3 Arduino Program Compilation & Upload](https://docs.m5stack.com/en/arduino/m5sticks3/program)
- [Battery](https://docs.m5stack.com/en/arduino/m5sticks3/battery)
- [Button](https://docs.m5stack.com/en/arduino/m5sticks3/button)
- [Display](https://docs.m5stack.com/en/arduino/m5sticks3/display)
- [IMU](https://docs.m5stack.com/en/arduino/m5sticks3/imu)
- [IR NEC](https://docs.m5stack.com/en/arduino/m5sticks3/ir_nec)
- [Mic](https://docs.m5stack.com/en/arduino/m5sticks3/mic)
- [Speaker](https://docs.m5stack.com/en/arduino/m5sticks3/speaker)
- [Wakeup](https://docs.m5stack.com/en/arduino/m5sticks3/wakeup)
- [StickS3 Low-Power Configuration](https://docs.m5stack.com/en/arduino/m5sticks3/m5pm1)
- [M5PM1 Library](https://github.com/m5stack/M5PM1)
