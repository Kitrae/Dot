// =============================================================================
// Dot Engine - Vec2
// =============================================================================
// 2D vector (no SIMD optimization - too small to benefit).
// =============================================================================

#pragma once

#include "Core/Core.h"

#include <cmath>

namespace Dot
{

constexpr float kVec2Epsilon = 1e-6f;

struct Vec2
{
    float x, y;

    // Constructors
    constexpr Vec2() : x(0), y(0) {}
    constexpr Vec2(float scalar) : x(scalar), y(scalar) {}
    constexpr Vec2(float x, float y) : x(x), y(y) {}

    // Static constructors
    static constexpr Vec2 Zero() { return Vec2(0, 0); }
    static constexpr Vec2 One() { return Vec2(1, 1); }
    static constexpr Vec2 UnitX() { return Vec2(1, 0); }
    static constexpr Vec2 UnitY() { return Vec2(0, 1); }

    // Operators
    constexpr Vec2 operator+(const Vec2& v) const { return Vec2(x + v.x, y + v.y); }
    constexpr Vec2 operator-(const Vec2& v) const { return Vec2(x - v.x, y - v.y); }
    constexpr Vec2 operator*(const Vec2& v) const { return Vec2(x * v.x, y * v.y); }
    constexpr Vec2 operator/(const Vec2& v) const { return Vec2(x / v.x, y / v.y); }
    constexpr Vec2 operator*(float s) const { return Vec2(x * s, y * s); }
    constexpr Vec2 operator/(float s) const { return Vec2(x / s, y / s); }
    constexpr Vec2 operator-() const { return Vec2(-x, -y); }

    Vec2& operator+=(const Vec2& v)
    {
        x += v.x;
        y += v.y;
        return *this;
    }
    Vec2& operator-=(const Vec2& v)
    {
        x -= v.x;
        y -= v.y;
        return *this;
    }
    Vec2& operator*=(const Vec2& v)
    {
        x *= v.x;
        y *= v.y;
        return *this;
    }
    Vec2& operator*=(float s)
    {
        x *= s;
        y *= s;
        return *this;
    }
    Vec2& operator/=(float s)
    {
        x /= s;
        y /= s;
        return *this;
    }

    constexpr bool operator==(const Vec2& v) const { return x == v.x && y == v.y; }
    constexpr bool operator!=(const Vec2& v) const { return !(*this == v); }

    // Indexing
    float& operator[](int i) { return (&x)[i]; }
    const float& operator[](int i) const { return (&x)[i]; }

    // Methods
    float Length() const { return std::sqrt(x * x + y * y); }
    float LengthSquared() const { return x * x + y * y; }

    Vec2 Normalized() const
    {
        float len = Length();
        return len > kVec2Epsilon ? *this / len : Vec2::Zero();
    }

    void Normalize()
    {
        float len = Length();
        if (len > kVec2Epsilon)
        {
            x /= len;
            y /= len;
        }
    }

    static float Dot(const Vec2& a, const Vec2& b) { return a.x * b.x + a.y * b.y; }

    static float Cross(const Vec2& a, const Vec2& b) { return a.x * b.y - a.y * b.x; }

    static float Distance(const Vec2& a, const Vec2& b) { return (b - a).Length(); }

    static Vec2 Lerp(const Vec2& a, const Vec2& b, float t)
    {
        return Vec2(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t);
    }

    bool ApproxEqual(const Vec2& v, float epsilon = kVec2Epsilon) const
    {
        return std::abs(x - v.x) < epsilon && std::abs(y - v.y) < epsilon;
    }
};

// Scalar * Vec2
inline constexpr Vec2 operator*(float s, const Vec2& v)
{
    return v * s;
}

} // namespace Dot
