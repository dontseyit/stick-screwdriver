#include "services/ImuService.h"

#include "core/Log.h"

#include <M5Unified.h>

namespace sd {
namespace {

constexpr uint8_t kChipId  = 0x00;  ///< reads 0x24 on a BMI270
constexpr uint8_t kAccConf = 0x40;  ///< [7] filter perf, [6:4] bandwidth, [3:0] ODR

/// Finds the sensor rather than assuming where it is. The BMI270's address
/// depends on how its SDO pin is strapped, and a hard-coded guess that misses
/// fails silently: the write returns an error nobody looks at and the rate
/// stays put, which looks like the feature not existing.
uint8_t findBmi270() {
    for (const uint8_t addr : {0x68, 0x69}) {
        uint8_t id = 0;
        if (M5.In_I2C.readRegister(addr, kChipId, &id, 1, 400000) && id == 0x24)
            return addr;
    }
    return 0;
}

uint8_t odrNibble(uint16_t hz) {
    if (hz >= 1600) return 0x0C;
    if (hz >= 800)  return 0x0B;
    if (hz >= 400)  return 0x0A;
    if (hz >= 200)  return 0x09;
    return 0x08;  // 100 Hz, the power-on default
}

uint16_t odrHz(uint8_t nibble) {
    switch (nibble) {
        case 0x0C: return 1600;
        case 0x0B: return 800;
        case 0x0A: return 400;
        case 0x09: return 200;
        default:   return 100;
    }
}

}  // namespace

bool ImuService::setSampleRate(uint16_t hz) {
    if (M5.Imu.getType() != m5::imu_bmi270) return false;
    if (_addr == 0) _addr = findBmi270();
    if (_addr == 0) {
        SD_LOGW("imu", "BMI270 not found on the internal bus; rate stays %u",
                static_cast<unsigned>(_configuredHz));
        return false;
    }

    uint8_t conf = 0;
    if (!M5.In_I2C.readRegister(_addr, kAccConf, &conf, 1, 400000)) {
        SD_LOGW("imu", "ACC_CONF read failed");
        return false;
    }
    const uint8_t want = static_cast<uint8_t>((conf & 0xF0) | odrNibble(hz));
    if (want != conf && !M5.In_I2C.writeRegister8(_addr, kAccConf, want, 400000)) {
        SD_LOGW("imu", "ACC_CONF write failed");
        return false;
    }

    _configuredHz = odrHz(want & 0x0F);
    SD_LOGI("imu", "accel rate %u Hz (0x%02x @ 0x%02x)",
            static_cast<unsigned>(_configuredHz), want, _addr);
    return true;
}

bool ImuService::begin() {
    _ok = (M5.Imu.getType() != m5::imu_none);
    if (!_ok) {
        SD_LOGE("imu", "no IMU detected");
        return false;
    }
    // M5Unified's auto-offset trim continuously nudges the zero point, which is
    // exactly wrong for an instrument: it would slowly absorb a real tilt and
    // call it bias. The level does its own deliberate calibration instead.
    M5.Imu.setCalibration(0, 0, 0);
    // Assert the rate rather than assume it. The sensor keeps its configuration
    // across everything short of a power cycle, so an app that raised it and
    // exited would leave the level and the shock logger reading a sensor whose
    // rate their filter constants were never written for.
    setSampleRate(100);
    _samples = 0;
    _rateHz  = 0.0f;
    _lastUs  = micros();
    SD_LOGI("imu", "started (%s)", sensorName());
    return true;
}

void ImuService::end() {
    setSampleRate(100);
    _ok = false;
}

bool ImuService::poll() {
    if (!_ok) return false;
    if (!M5.Imu.update()) return false;

    const auto& d = M5.Imu.getImuData();
    _accel = {d.accel.x, d.accel.y, d.accel.z};
    _gyro  = {d.gyro.x, d.gyro.y, d.gyro.z};

    const uint32_t now = micros();
    const uint32_t dt  = now - _lastUs;
    _lastUs            = now;
    if (dt > 0 && dt < 1000000u) {
        const float inst = 1000000.0f / static_cast<float>(dt);
        _rateHz = (_rateHz == 0.0f) ? inst : (_rateHz * 0.95f + inst * 0.05f);
    }
    ++_samples;
    return true;
}

const char* ImuService::sensorName() const {
    switch (M5.Imu.getType()) {
        case m5::imu_bmi270:  return "BMI270";
        case m5::imu_mpu6886: return "MPU6886";
        case m5::imu_mpu6050: return "MPU6050";
        case m5::imu_mpu9250: return "MPU9250";
        case m5::imu_sh200q:  return "SH200Q";
        default:              return "none";
    }
}

}  // namespace sd
