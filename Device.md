
# Board

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

# Infrared Reception Notes
1.The infrared receiving and decoding of StickS3 must use the ESP32 RMT peripheral and does not support receiving and decoding via GPIO method.
2. When using the infrared receiver function, the speaker amplifier must be turned off; otherwise, reception will not work properly. The operation method can be referenced in the tutorial
3. Ensure that the transmitter and receiver are aligned as directly as possible, and keep a distance of no less than 30 cm between them. If the distance is too short, abnormal reception may occur.

# Speaker Volume Notice
When powered by battery (USB not connected), it is recommended to keep the speaker volume below 75% to avoid unexpected device reboot caused by excessive power consumption.

EXT_5V_EN

In the default initialization of M5Unified, EXT_5V_EN is disabled. This operation turns off the power supply to the Grove, Hat EXT_5V interfaces, and IR TX/RX, and switches them to input mode. In this state, an external 5V power input is required for IR TX/RX to operate properly. For use cases without an external power supply, you can re-enable the EXT_5V output mode through the following API to restore power to IR TX/RX.

M5.Power.setExtOutput(true); // EXT_5V OUTPUT
// M5.Power.setExtOutput(false); // EXT_5V INPUT

# PinMap

## LCD
ESP32‑S3,G39,G40,G45,G41,G21,G38
ST7789P3,MOSI,SCK,RS,CS,RST,BL

## IMU & M5PM1
ESP32‑S3,G48,G47
BMI270 (0x68),SCL,SDA
M5PM1 (0x6e),SCL,SDA

## M5PM1
M5PM1,G0,G1,G2,G3,G4
Battery Charge,PYG0_CHG_STAT	
ESP32-S3,,G13
L3B Power,,,PYG2_L3B_EN
Speaker,,,,PYG3_SPK_Pulse
IMU INT,,,,,PYG4_IMU_INT

## Audio
ESP32‑S3,G18,G14,G17,G15,G16,G48,G47
ES8311 (0x18),MCLK,DOUT,BCLK,LRCK,DIN,SCL,SDA

## Button
ESP32‑S3,G11,G12
Input,KEY1,KEY2

## IR
ESP32‑S3,G46,G42
IR,IR_TX,IR_RX

## Hat2 bus (2.54-16P)
Exposed GPIO,G0,G1,G2,G3,G4,G5,G6,G7,G8,G43,G44
Also on header,EXT_5V,5VIN,3V3,GND

G1-G8 are ADC1 channels 0-7. ADC1 matters: ADC2 shares hardware with the radio
and cannot be read while WiFi is up.

Do not use for input:
  G0   Boot button, strapping pin
  G3   strapping pin - selects the JTAG source at reset
  G43  serial console TX (U0TXD)
  G44  serial console RX (U0RXD)

## HY2.0-4P
PORT.A
HY2.0-4P,Black,Red,Yellow,White
PORT.CUSTOM,GND,5V,G9,G10

# Official Examples and Libraries

- [StickS3 Arduino Program Compilation & Upload](https://docs.m5stack.com/en/arduino/m5sticks3/program)
- [Battery](https://docs.m5stack.com/en/arduino/m5sticks3/battery)
- [Button](https://docs.m5stack.com/en/arduino/m5sticks3/button)
- [Display](https://docs.m5stack.com/en/arduino/m5sticks3/display)
- [IMU](https://docs.m5stack.com/en/arduino/m5sticks3/imu)
- [IR NEC](https://docs.m5stack.com/en/arduino/m5sticks3/ir_nec)
- [Mic](https://docs.m5stack.com/en/arduino/m5sticks3/mic)
- [Speaker](https://Did youdocs.m5stack.com/en/arduino/m5sticks3/speaker)
- [Wakeup](https://docs.m5stack.com/en/arduino/m5sticks3/wakeup)
- [StickS3 Low-Power Configuration](https://docs.m5stack.com/en/arduino/m5sticks3/m5pm1)
- [M5PM1 Library](https://github.com/m5stack/M5PM1)
