// =============================================================================
// Dot Engine - Quaternion
// =============================================================================
// Unit quaternion for rotations (x, y, z, w) where w is the scalar part.
// =============================================================================

#pragma once

#include "Core/Math/Vec3.h"
#include "Core/Math/Vec4.h"

#include <cmath>

namespace Dot
{

constexpr float kQuatEpsilon = 1e-6f;
constexpr float kQuatHalfPi = 1.57079632679489661923f;

template <typename T> constexpr T QuatClamp(T val, T minVal, T maxVal)
{
    return val < minVal ? minVal : (val > maxVal ? maxVal : val);
}

struct Mat4; // Forward declaration

struct Quat
{
    float x, y, z, w;

    // Constructors
    Quat() : x(0), y(0), z(0), w(1) {} // Identity quaternion
    Quat(float x, float y, float z, float w) : x(x), y(y), z(z), w(w) {}

    // Static constructors
    static Quat Identity() { return Quat(0, 0, 0, 1); }

    static Quat FromAxisAngle(const Vec3& axis, float radians)
    {
        float halfAngle = radians * 0.5f;
        float s = std::sin(halfAngle);
        Vec3 n = axis.Normalized();
        return Quat(n.x * s, n.y * s, n.z * s, std::cos(halfAngle));
    }

    static Quat FromEuler(float pitch, float yaw, float roll)
    {
        float cy = std::cos(yaw * 0.5f);
        float sy = std::sin(yaw * 0.5f);
        float cp = std::cos(pitch * 0.5f);
        float sp = std::sin(pitch * 0.5f);
        float cr = std::cos(roll * 0.5f);
        float sr = std::sin(roll * 0.5f);

        return Quat(sr * cp * cy - cr * sp * sy, cr * sp * cy + sr * cp * sy, cr * cp * sy - sr * sp * cy,
                    cr * cp * cy + sr * sp * sy);
    }

    static Quat FromEuler(const Vec3& euler) { return FromEuler(euler.x, euler.y, euler.z); }

    // Operators
    Quat operator*(const Quat& q) const
    {
        return Quat(w * q.x + x * q.w + y * q.z - z * q.y, w * q.y - x * q.z + y * q.w + z * q.x,
                    w * q.z + x * q.y - y * q.x + z * q.w, w * q.w - x * q.x - y * q.y - z * q.z);
    }

    Quat operator*(float s) const { return Quat(x * s, y * s, z * s, w * s); }
    Quat operator+(const Quat& q) const { return Quat(x + q.x, y + q.y, z + q.z, w + q.w); }
    Quat operator-() const { return Quat(-x, -y, -z, -w); }

    bool operator==(const Quat& q) const { return x == q.x && y == q.y && z == q.z && w == q.w; }
    bool operator!=(const Quat& q) const { return !(*this == q); }

    // Rotate a vector
    Vec3 operator*(const Vec3& v) const
    {
        Vec3 qv(x, y, z);
        Vec3 uv = Vec3::Cross(qv, v);
        Vec3 uuv = Vec3::Cross(qv, uv);
        return v + (uv * w + uuv) * 2.0f;
    }

    // Methods
    float Length() const { return std::sqrt(x * x + y * y + z * z + w * w); }
    float LengthSquared() const { return x * x + y * y + z * z + w * w; }

    Quat Normalized() const
    {
        float len = Length();
        if (len > kQuatEpsilon)
        {
            float invLen = 1.0f / len;
            return Quat(x * invLen, y * invLen, z * invLen, w * invLen);
        }
        return Identity();
    }

    void Normalize() { *this = Normalized(); }
    Quat Conjugate() const { return Quat(-x, -y, -z, w); }

    Quat Inverse() const
    {
        float lenSq = LengthSquared();
        if (lenSq > kQuatEpsilon)
        {
            float invLenSq = 1.0f / lenSq;
            return Quat(-x * invLenSq, -y * invLenSq, -z * invLenSq, w * invLenSq);
        }
        return Identity();
    }

    static float Dot(const Quat& a, const Quat& b) { return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w; }

    static Quat Lerp(const Quat& a, const Quat& b, float t)
    {
        return Quat(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t)
            .Normalized();
    }

    static Quat Slerp(const Quat& a, const Quat& b, float t)
    {
        Quat q = b;
        float dot = Dot(a, q);

        if (dot < 0.0f)
        {
            q = -q;
            dot = -dot;
        }
        if (dot > 0.9995f)
        {
            return Lerp(a, q, t);
        }

        float theta = std::acos(QuatClamp(dot, -1.0f, 1.0f));
        float sinTheta = std::sin(theta);
        float wa = std::sin((1.0f - t) * theta) / sinTheta;
        float wb = std::sin(t * theta) / sinTheta;

        return Quat(a.x * wa + q.x * wb, a.y * wa + q.y * wb, a.z * wa + q.z * wb, a.w * wa + q.w * wb);
    }

    Vec3 ToEuler() const
    {
        Vec3 euler;
        float sinr_cosp = 2.0f * (w * x + y * z);
        float cosr_cosp = 1.0f - 2.0f * (x * x + y * y);
        euler.x = std::atan2(sinr_cosp, cosr_cosp);

        float sinp = 2.0f * (w * y - z * x);
        if (std::abs(sinp) >= 1.0f)
            euler.y = std::copysign(kQuatHalfPi, sinp);
        else
            euler.y = std::asin(sinp);

        float siny_cosp = 2.0f * (w * z + x * y);
        float cosy_cosp = 1.0f - 2.0f * (y * y + z * z);
        euler.z = std::atan2(siny_cosp, cosy_cosp);

        return euler;
    }

    bool ApproxEqual(const Quat& q, float epsilon = kQuatEpsilon) const
    {
        return std::abs(x - q.x) < epsilon && std::abs(y - q.y) < epsilon && std::abs(z - q.z) < epsilon &&
               std::abs(w - q.w) < epsilon;
    }
};

} // namespace Dot
