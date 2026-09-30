#pragma once
// -----------------------------------------------------------------------------
//  ImuService - the BMI270, wrapped.
//
//  Apps see plain Vec3 values in physical units and a monotonic sample counter.
//  Nothing above this line knows the part number, so swapping the IMU or adding
//  a magnetometer is a change to one file.
// -----------------------------------------------------------------------------
#include "mathx/Vec3.h"

#include <cstdint>

namespace sd {

class ImuService {
public:
    bool begin();
    void end();

    bool available() const { return _ok; }

    /// Fetches a sample if the sensor has one ready. Returns true on new data.
    bool poll();

    const Vec3& accel() const { return _accel; }  ///< g
    const Vec3& gyro() const  { return _gyro; }   ///< deg/s

    uint32_t samples() const   { return _samples; }
    uint32_t lastSampleUs() const { return _lastUs; }

    /// Measured update rate, for confirming the filter is fed at the rate its
    /// constants assume.
    float rateHz() const { return _rateHz; }

    /// Sets the accelerometer's output data rate.
    ///
    /// M5Unified leaves the BMI270 at its power-on 100 Hz and offers no way to
    /// change it, which caps any spectrum at 50 Hz, below most of a 3D
    /// printer's frame resonances. The rate lives in the low nibble of
    /// ACC_CONF, so this reads that register, replaces the nibble and writes it
    /// back, leaving the part's own filter selection alone: that filter is
    /// chosen for the ODR and is what stops energy above Nyquist folding back.
    ///
    /// Accepts 100, 200, 400, 800 or 1600 and rounds down. begin() asserts
    /// 100 Hz, because the sensor keeps its configuration until something
    /// changes it, and an app that raised the rate and exited would leave every
    /// other app reading a sensor it does not expect.
    bool setSampleRate(uint16_t hz);
    uint16_t sampleRateHz() const { return _configuredHz; }

    const char* sensorName() const;

private:
    Vec3     _accel{0, 0, 1};
    Vec3     _gyro{};
    uint32_t _samples = 0;
    uint32_t _lastUs  = 0;
    float    _rateHz  = 0.0f;
    uint16_t _configuredHz = 100;
    uint8_t  _addr         = 0;   ///< found, not assumed
    bool     _ok      = false;
};

}  // namespace sd
