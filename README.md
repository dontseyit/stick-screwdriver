# Screwdriver

Firmware for the **M5Stack StickS3**: a framework for pocket tools, and the
four that come with it. Apps are added one file at a time and no central list
is edited, so a tool you write cannot break the ones already there.

This repository is the base. SCREWDRIVER (the launcher), SYSTEM, HELP and
CONSOLE are here; other apps install into it.

## Hardware

| | |
|---|---|
| SoC | ESP32-S3-PICO-1-N8R8, dual-core LX7 @ 240 MHz |
| Memory | 8 MB flash, 8 MB octal PSRAM |
| Display | 1.14" ST7789P3, 135x240 |
| IMU | BMI270 @ I2C 0x68 |
| Power | M5PM1 @ I2C 0x6e, 250 mAh cell |
| Audio | ES8311 codec, AW8737 amp, MEMS mic |
| IR | TX G46 / RX G42 |
| Buttons | KEY1 G11, KEY2 G12 |
| Expansion | Hat2 bus, HY2.0-4P Port A |

Pin map and the board's quirks: [Device.md](Device.md). Battery and sleep:
[Power.md](Power.md).

## Build

Install [PlatformIO](https://platformio.org/install/cli), then:

```bash
pio run -e sticks3            # build
pio run -e sticks3 -t upload  # flash
pio device monitor            # 115200, exception decoder on
pio test -e native            # the hardware-free maths, on your laptop
```

## CONSOLE

CONSOLE puts the device on WiFi and serves its own pages. The front page
reports the cell, the die temperatures and free memory, and lists whatever
other pages the apps you installed registered.

The access point is open and anything served is readable by anyone in range.
Turn it off when you are done.

## Adding an app

One file, and no other file changes. See [docs/writing-apps.md](docs/writing-apps.md)
for the whole of it; the shape is:

```cpp
class MyApp : public sd::App { /* onStart, onEvent, onTick, onDraw */ };

const sd::AppInfo kInfo{ .id = "mine", .title = "MINE", .needs = sd::caps(sd::Cap::Imu) };
SD_REGISTER_APP(MyApp, kInfo)
```

Drop the directory into `src/apps/`, build, and it is in the launcher.

How the framework fits together, and what each layer owes the others:
[docs/architecture.md](docs/architecture.md).

## Licence

MIT. See [LICENSE](LICENSE).
