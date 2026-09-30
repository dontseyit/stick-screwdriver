# Power management reference: M5PM1 PMIC on the StickS3

Collected 2026-09-11 from docs.m5stack.com/en/arduino/m5sticks3/m5pm1 and /battery, the M5PM1 Arduino library 1.0.6 (github.com/m5stack/M5PM1, README_FUNCTION_EN.md), the M5PM1 Chip User Manual v1.9 (m5stack-doc.oss-cn-shenzhen.aliyuncs.com/1207/M5PM1_Datasheet_EN.pdf) and M5Unified 0.2.21 `utility/power/M5PM1_Class.*`. Where the sources disagree, the manual wins.

## In this project

- The PMIC sits on I2C SDA 47 / SCL 48 at 0x6E, 100 kHz (400 kHz switchable). M5GFX owns that bus as `I2C_NUM_1` and already talks to the PMIC at init (probe, L3B on, `I2C_CFG = 0`). Talk to it through `m5gfx::i2c::*` on `I2C_NUM_1` or M5Unified's `M5PM1_Class` over `In_I2C`. Do not call `Wire`/`Wire1.begin(47, 48)` or the M5PM1 library's `begin(TwoWire*, ...)` / `begin(i2c_port_t, ...)`: a second driver on the same pins breaks both. The M5PM1 library's `begin(m5::I2C_Class*, addr, speed)` overload is the compatible one; include M5Unified before M5PM1.
- Firmware that never touches the PMIC is safe: watchdog off by default, no power-hold pin, L0/L1/L2 on at PMIC start, L3B switched on by M5GFX.
- Battery mode limits: speaker volume under 75 % (Device.md), and USB/5VOUT readings under 4 V count as absent.

## Power tree

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

## Power button

The side button is `PWR_BTN` on the PMIC, not an ESP32 GPIO.

- Single click: system reset through `BOOT_OUT`. Disable with `setSingleResetDisable(true)` ("high risk"); a single click then raises IRQ Status 3 bit 0 instead.
- Double click: power off to L0. Disable with `setDoubleOffDisable(true)` ("high risk"); a double click then raises IRQ Status 3 bit 2.
- Download mode: `enterDownloadMode()` (`SYS_CMD`), lockable with `setDownloadLock(true)`. Entering it clears `HOLD_CFG`, stops the watchdog, I2C sleep and timer, and returns GPIOs and rails to defaults.
- LED (`PWR_CFG` bit 4, `setLedEnLevel()`): one flash on reset; 500 ms blink in download mode; 200 ms blink when button reset is disabled and a GPIO IRQ is enabled; 100 ms blink when double-click off is disabled and a GPIO IRQ is enabled.
- None of the sources read gives press durations for power-on; treat any "hold for N seconds" figure as unverified.

## Battery, charging, power source

- M5Unified: `M5.Power.getBatteryVoltage()` (mV), `getBatteryLevel()` (%), `isCharging()` (StickS3 reads PMIC GPIO0 = `CHG_STAT`, low = charging), `getVBUSVoltage()`. `M5PM1_Class` adds `setBatteryCharge()`, `setChargeCurrent(mA)`, `setChargeVoltage(mV)`, `getPowerSource()`, `get5VoutVoltage()`.
- Registers, 16-bit little-endian millivolts, 1:1 dividers: `VREF` 0x20, `VBAT` 0x22, `VIN` 0x24, `5VOUT` 0x26; raw ADC result 0x28 (12 bit), `ADC_CTRL` 0x2A. `readTemperature()` exists in the M5PM1 library.
- `PWR_SRC` 0x04: bit 0 5VIN valid, bit 1 5VINOUT valid (0 while the boost is on), bit 2 VBAT node valid. "Tracks the node voltage, not physical battery presence."
- Charging is enabled by default and "not affected by reset" (`PWR_CFG` bit 0). `BATT_LVP` 0x08 is the low-voltage protection threshold, default 0x40; `setBatteryLvp()` in the M5PM1 library (units not stated in the extracted text).
- Insertion IRQs: battery insert/remove only fires while charging is disabled; 5VIN/OUT insert/remove only fires in INPUT mode.

## Shutdown, timer, wake

- `shutdown()` writes `SYS_CMD` 0x0C = 0xA1 and drops to L0. Rails and GPIOs whose `HOLD_CFG` (0x07) bit is set keep their state: bit 6 5VIN/OUT, bit 5 LDO, bits 4:0 GPIO4..0 (`ldoSetPowerHold`, `boostSetPowerHold`, `gpioSetPowerHold`). `HOLD_CFG` is cleared by download mode and by every reset.
- Timer: `timerSet(seconds, action)` with `STOP`, `FLAG`, `REBOOT`, `POWERON`, `POWEROFF`; `timerClear()`. POWERON also works from the powered-off state. The timer is cleared by power-off.
- Wake sources are flagged in `WAKE_SRC` 0x05 (clear with `clearWakeSource()`). GPIO wake: `gpioSetWakeEnable(pin, true)`, `gpioSetWakeEdge(pin, edge)`; GPIO0/GPIO2 are mutually exclusive as wake pins, so are GPIO3/GPIO4.
- Motion wake from shutdown: keep the IMU alive, then shut down: `setLdoEnable(true); ldoSetPowerHold(true); setLedEnLevel(true); shutdown();` IMU INT1 on PYG4 wakes the PMIC.
- Motion wake of a deep-sleeping ESP32: PYG4 (IMU INT1 in) to PYG1 as IRQ output to ESP32 GPIO 13. Configure PYG4 input with pull-up, PYG1 output push-pull with `FUNC = IRQ`, unmask only the GPIO4 IRQ, then `esp_sleep_enable_ext0_wakeup(GPIO_NUM_13, 0); rtc_gpio_pullup_en(GPIO_NUM_13); esp_deep_sleep_start();`.
- I2C idle sleep: `I2C_CFG` 0x09 bits 3:0 `SLP_TO`, 0 = off (`setI2cSleepTime()`). The first transaction after sleep fails and wakes the chip; retry. Ineffective while PWM is enabled or in download mode. M5GFX and M5Unified write `I2C_CFG = 0x00`.
- Watchdog: `WDT_CNT` 0x0A counts down in seconds, 0 = disabled (default since firmware 5); feed by writing 0xA5 to `WDT_KEY` 0x0B (`wdtSet`, `wdtFeed`).

## PMIC GPIOs (PYG0..4)

| Pin | Wired to | Alternate functions |
| --- | --- | --- |
| PYG0 | `CHG_STAT` input, low = charging | NeoPixel out; wake (exclusive with PYG2) |
| PYG1 | IRQ output to ESP32 GPIO 13 | ADC1 |
| PYG2 | L3B enable (LCD, mic, speaker) | ADC2; wake (exclusive with PYG0) |
| PYG3 | speaker amp enable, AW8737 pulse | PWM0; wake (exclusive with PYG4) |
| PYG4 | IMU INT1 input | PWM1 |

GPIO registers: `MODE` 0x10, `OUT` 0x11, `IN` 0x12, `DRV` 0x13 (default 0x1F = all open-drain; 0 = push-pull), `PUPD` 0x14/0x15, `FUNC` 0x16/0x17 (00 GPIO, 01 IRQ, 11 special). Outputs only take effect with `FUNC = 00`. PWM frequency is shared by both channels (0x34), duty 0x30, 12 bit.

## Register map

`Device_ID` 0x00 = 0x50, `Device_Model` 0x01 = 0x20, `HW_REV` 0x02, `SW_REV` 0x03, `PWR_SRC` 0x04, `WAKE_SRC` 0x05, `PWR_CFG` 0x06 (default 0x17: LED, LDO, DCDC and charging on, 5 V interface as input), `HOLD_CFG` 0x07, `BATT_LVP` 0x08, `I2C_CFG` 0x09, `WDT_CNT` 0x0A, `WDT_KEY` 0x0B, `SYS_CMD` 0x0C (key 0xA in bits 7:4), GPIO 0x10-0x17, ADC 0x20-0x2A, PWM 0x30-0x35, IRQ status 0x40-0x42 and masks 0x43-0x45, `BTN_Status` 0x48, NeoPixel 0x50 and 0x60-0x9F, RTC RAM 0xA0-0xBF. IRQ mask setters take single items only; use the `*All` variants for ALL/NONE.
