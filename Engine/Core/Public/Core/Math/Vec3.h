// =============================================================================
// Dot Engine - Vec3
// =============================================================================
// 3D vector with optional SIMD. Often stored as Vec4 internally for alignment.
// =============================================================================

#pragma once

#include "Core/Core.h"

#include <cmath>

namespace Dot
{

constexpr float kVec3Epsilon = 1e-6f;

struct Vec4; // Forward declaration

struct Vec3
{
    float x, y, z;

    // Constructors
    constexpr Vec3() : x(0), y(0), z(0) {}
    constexpr Vec3(float scalar) : x(scalar), y(scalar), z(scalar) {}
    constexpr Vec3(float x, float y, float z) : x(x), y(y), z(z) {}

    // Static constructors
    static constexpr Vec3 Zero() { return Vec3(0, 0, 0); }
    static constexpr Vec3 One() { return Vec3(1, 1, 1); }
    static constexpr Vec3 UnitX() { return Vec3(1, 0, 0); }
    static constexpr Vec3 UnitY() { return Vec3(0, 1, 0); }
    static constexpr Vec3 UnitZ() { return Vec3(0, 0, 1); }
    static constexpr Vec3 Up() { return Vec3(0, 1, 0); }
    static constexpr Vec3 Right() { return Vec3(1, 0, 0); }
    static constexpr Vec3 Forward() { return Vec3(0, 0, -1); } // RH coordinate system

    // Operators
    constexpr Vec3 operator+(const Vec3& v) const { return Vec3(x + v.x, y + v.y, z + v.z); }
    constexpr Vec3 operator-(const Vec3& v) const { return Vec3(x - v.x, y - v.y, z - v.z); }
    constexpr Vec3 operator*(const Vec3& v) const { return Vec3(x * v.x, y * v.y, z * v.z); }
    constexpr Vec3 operator/(const Vec3& v) const { return Vec3(x / v.x, y / v.y, z / v.z); }
    constexpr Vec3 operator*(float s) const { return Vec3(x * s, y * s, z * s); }
    constexpr Vec3 operator/(float s) const { return Vec3(x / s, y / s, z / s); }
    constexpr Vec3 operator-() const { return Vec3(-x, -y, -z); }

    Vec3& operator+=(const Vec3& v)
    {
        x += v.x;
        y += v.y;
        z += v.z;
        return *this;
    }
    Vec3& operator-=(const Vec3& v)
    {
        x -= v.x;
        y -= v.y;
        z -= v.z;
        return *this;
    }
    Vec3& operator*=(const Vec3& v)
    {
        x *= v.x;
        y *= v.y;
        z *= v.z;
        return *this;
    }
    Vec3& operator*=(float s)
    {
        x *= s;
        y *= s;
        z *= s;
        return *this;
    }
    Vec3& operator/=(float s)
    {
        x /= s;
        y /= s;
        z /= s;
        return *this;
    }

    constexpr bool operator==(const Vec3& v) const { return x == v.x && y == v.y && z == v.z; }
    constexpr bool operator!=(const Vec3& v) const { return !(*this == v); }

    // Indexing
    float& operator[](int i) { return (&x)[i]; }
    const float& operator[](int i) const { return (&x)[i]; }

    // Methods
    float Length() const { return std::sqrt(x * x + y * y + z * z); }
    float LengthSquared() const { return x * x + y * y + z * z; }

    Vec3 Normalized() const
    {
        float len = Length();
        return len > kVec3Epsilon ? *this / len : Vec3::Zero();
    }

    void Normalize()
    {
        float len = Length();
        if (len > kVec3Epsilon)
        {
            x /= len;
            y /= len;
            z /= len;
        }
    }

    static float Dot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

    static Vec3 Cross(const Vec3& a, const Vec3& b)
    {
        return Vec3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x);
    }

    static float Distance(const Vec3& a, const Vec3& b) { return (b - a).Length(); }

    static Vec3 Lerp(const Vec3& a, const Vec3& b, float t)
    {
        return Vec3(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t);
    }

    static Vec3 Reflect(const Vec3& incident, const Vec3& normal)
    {
        return incident - normal * (2.0f * Dot(incident, normal));
    }

    bool ApproxEqual(const Vec3& v, float epsilon = kVec3Epsilon) const
    {
        return std::abs(x - v.x) < epsilon && std::abs(y - v.y) < epsilon && std::abs(z - v.z) < epsilon;
    }
};

// Scalar * Vec3
inline constexpr Vec3 operator*(float s, const Vec3& v)
{
    return v * s;
}

} // namespace Dot
