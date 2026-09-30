#pragma once
// -----------------------------------------------------------------------------
//  Vec3 - minimal 3D vector maths. Free of any Arduino or ESP-IDF dependency,
//  so the inclinometer maths can be tested on the host (`pio test -e native`).
//  maths can be unit-tested on the host (`pio test -e native`).
// -----------------------------------------------------------------------------
#include <cmath>

namespace sd {

struct Vec3 {
    float x = 0.0f, y = 0.0f, z = 0.0f;

    constexpr Vec3() = default;
    constexpr Vec3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}

    float&       operator[](int i)       { return (&x)[i]; }
    const float& operator[](int i) const { return (&x)[i]; }

    constexpr Vec3 operator+(const Vec3& o) const { return {x + o.x, y + o.y, z + o.z}; }
    constexpr Vec3 operator-(const Vec3& o) const { return {x - o.x, y - o.y, z - o.z}; }
    constexpr Vec3 operator*(float s)       const { return {x * s, y * s, z * s}; }
    constexpr Vec3 operator/(float s)       const { return {x / s, y / s, z / s}; }
    constexpr Vec3 operator-()              const { return {-x, -y, -z}; }

    Vec3& operator+=(const Vec3& o) { x += o.x; y += o.y; z += o.z; return *this; }
    Vec3& operator-=(const Vec3& o) { x -= o.x; y -= o.y; z -= o.z; return *this; }
    Vec3& operator*=(float s)       { x *= s;   y *= s;   z *= s;   return *this; }

    /// Element-wise product / quotient (used for per-axis gain correction).
    constexpr Vec3 scaled(const Vec3& g) const { return {x * g.x, y * g.y, z * g.z}; }
    constexpr Vec3 divided(const Vec3& g) const { return {x / g.x, y / g.y, z / g.z}; }

    constexpr float dot(const Vec3& o) const { return x * o.x + y * o.y + z * o.z; }

    constexpr Vec3 cross(const Vec3& o) const {
        return {y * o.z - z * o.y, z * o.x - x * o.z, x * o.y - y * o.x};
    }

    float norm()  const { return std::sqrt(dot(*this)); }
    constexpr float norm2() const { return dot(*this); }

    /// Unit vector. Returns {0,0,1} for a degenerate vector, so callers never
    /// have to special-case NaN.
    Vec3 normalized() const {
        const float n = norm();
        return (n > 1e-9f) ? (*this / n) : Vec3{0.0f, 0.0f, 1.0f};
    }

    /// Index (0..2) of the component with the largest magnitude.
    int dominantAxis() const {
        const float ax = std::fabs(x), ay = std::fabs(y), az = std::fabs(z);
        if (ax >= ay && ax >= az) return 0;
        return (ay >= az) ? 1 : 2;
    }
};

/// Numerically stable angle between two vectors, valid over the whole 0..180
/// deg range. atan2(|a x b|, a.b) beats acos(a.b) badly near 0 and 180, where
/// acos loses most of its significant digits.
inline float angleBetween(const Vec3& a, const Vec3& b) {
    return std::atan2(a.cross(b).norm(), a.dot(b));
}

/// Rotate `v` around unit axis `axis` by `angle` radians (Rodrigues' formula).
inline Vec3 rotateAround(const Vec3& v, const Vec3& axis, float angle) {
    const float c = std::cos(angle), s = std::sin(angle);
    return v * c + axis.cross(v) * s + axis * (axis.dot(v) * (1.0f - c));
}

/// The minimal rotation taking unit vector `from` onto unit vector `to`,
/// applied to `v`. Used to re-reference readings to a zeroed orientation.
inline Vec3 rotateFromTo(const Vec3& v, const Vec3& from, const Vec3& to) {
    const Vec3  axis = from.cross(to);
    const float s    = axis.norm();
    const float c    = from.dot(to);
    if (s < 1e-7f) {
        // Parallel (nothing to do) or anti-parallel (180 deg about any
        // perpendicular axis; pick one deterministically).
        if (c > 0.0f) return v;
        const Vec3 perp = (std::fabs(from.x) < 0.9f) ? Vec3{1, 0, 0} : Vec3{0, 1, 0};
        return rotateAround(v, from.cross(perp).normalized(), 3.14159265358979f);
    }
    return rotateAround(v, axis / s, std::atan2(s, c));
}

/// Rotation rate about the WORLD vertical, from a body-frame gyro reading and
/// the measured up direction, signed so a clockwise turn is positive.
///
/// The negation is worth stating once: `up` points at the ceiling, so by the
/// right-hand rule a positive rate about it is a counterclockwise turn. Both RF
/// apps depend on this, and two copies of a sign convention would disagree.
inline float yawRateFrom(const Vec3& gyro, const Vec3& up) {
    return -gyro.dot(up.normalized());
}

constexpr float kPi     = 3.14159265358979323846f;
constexpr float kRadDeg = 180.0f / kPi;
constexpr float kDegRad = kPi / 180.0f;

constexpr float toDeg(float rad) { return rad * kRadDeg; }
constexpr float toRad(float deg) { return deg * kDegRad; }

}  // namespace sd
