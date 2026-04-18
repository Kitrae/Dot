// =============================================================================
// Dot Engine - Vec4
// =============================================================================
// 4D vector with SIMD acceleration (SSE on x86/x64).
// =============================================================================

#pragma once

#include "Core/Core.h"
#include "Core/Math/Vec3.h"

#include <cmath>

// SIMD includes
#if DOT_ARCH_X64 || DOT_ARCH_X86
    #include <emmintrin.h> // SSE2
    #include <xmmintrin.h> // SSE
    #define DOT_SIMD_SSE 1
#else
    #define DOT_SIMD_SSE 0
#endif

namespace Dot
{

constexpr float kVec4Epsilon = 1e-6f;

// Suppress MSVC warning C4201 (nameless struct/union) - standard for math types
#if DOT_COMPILER_MSVC
    #pragma warning(push)
    #pragma warning(disable : 4201)
#endif

struct alignas(16) Vec4
{
    union
    {
        struct
        {
            float x, y, z, w;
        };
        struct
        {
            float r, g, b, a;
        };
        float data[4];
#if DOT_SIMD_SSE
        __m128 simd;
#endif
    };

#if DOT_COMPILER_MSVC
    #pragma warning(pop)
#endif

    // Constructors
    Vec4() : x(0), y(0), z(0), w(0) {}
    Vec4(float scalar) : x(scalar), y(scalar), z(scalar), w(scalar) {}
    Vec4(float x, float y, float z, float w = 0.0f) : x(x), y(y), z(z), w(w) {}
    Vec4(const Vec3& v, float w = 0.0f) : x(v.x), y(v.y), z(v.z), w(w) {}

#if DOT_SIMD_SSE
    Vec4(__m128 v) : simd(v) {}
#endif

    // Static constructors
    static Vec4 Zero() { return Vec4(0, 0, 0, 0); }
    static Vec4 One() { return Vec4(1, 1, 1, 1); }
    static Vec4 UnitX() { return Vec4(1, 0, 0, 0); }
    static Vec4 UnitY() { return Vec4(0, 1, 0, 0); }
    static Vec4 UnitZ() { return Vec4(0, 0, 1, 0); }
    static Vec4 UnitW() { return Vec4(0, 0, 0, 1); }

    // Operators
#if DOT_SIMD_SSE
    Vec4 operator+(const Vec4& v) const { return Vec4(_mm_add_ps(simd, v.simd)); }
    Vec4 operator-(const Vec4& v) const { return Vec4(_mm_sub_ps(simd, v.simd)); }
    Vec4 operator*(const Vec4& v) const { return Vec4(_mm_mul_ps(simd, v.simd)); }
    Vec4 operator/(const Vec4& v) const { return Vec4(_mm_div_ps(simd, v.simd)); }
    Vec4 operator*(float s) const { return Vec4(_mm_mul_ps(simd, _mm_set1_ps(s))); }
    Vec4 operator/(float s) const { return Vec4(_mm_div_ps(simd, _mm_set1_ps(s))); }
#else
    Vec4 operator+(const Vec4& v) const { return Vec4(x + v.x, y + v.y, z + v.z, w + v.w); }
    Vec4 operator-(const Vec4& v) const { return Vec4(x - v.x, y - v.y, z - v.z, w - v.w); }
    Vec4 operator*(const Vec4& v) const { return Vec4(x * v.x, y * v.y, z * v.z, w * v.w); }
    Vec4 operator/(const Vec4& v) const { return Vec4(x / v.x, y / v.y, z / v.z, w / v.w); }
    Vec4 operator*(float s) const { return Vec4(x * s, y * s, z * s, w * s); }
    Vec4 operator/(float s) const { return Vec4(x / s, y / s, z / s, w / s); }
#endif
    Vec4 operator-() const { return Vec4(-x, -y, -z, -w); }

    Vec4& operator+=(const Vec4& v)
    {
        *this = *this + v;
        return *this;
    }
    Vec4& operator-=(const Vec4& v)
    {
        *this = *this - v;
        return *this;
    }
    Vec4& operator*=(const Vec4& v)
    {
        *this = *this * v;
        return *this;
    }
    Vec4& operator*=(float s)
    {
        *this = *this * s;
        return *this;
    }
    Vec4& operator/=(float s)
    {
        *this = *this / s;
        return *this;
    }

    bool operator==(const Vec4& v) const { return x == v.x && y == v.y && z == v.z && w == v.w; }
    bool operator!=(const Vec4& v) const { return !(*this == v); }

    // Indexing
    float& operator[](int i) { return data[i]; }
    const float& operator[](int i) const { return data[i]; }

    // Conversions
    Vec3 ToVec3() const { return Vec3(x, y, z); }

    // Methods
    float Length() const { return std::sqrt(x * x + y * y + z * z + w * w); }
    float LengthSquared() const { return x * x + y * y + z * z + w * w; }
    float Length3() const { return std::sqrt(x * x + y * y + z * z); } // Ignore w

    Vec4 Normalized() const
    {
        float len = Length();
        return len > kVec4Epsilon ? *this / len : Vec4::Zero();
    }

    void Normalize()
    {
        float len = Length();
        if (len > kVec4Epsilon)
        {
            *this = *this / len;
        }
    }

    static float Dot(const Vec4& a, const Vec4& b)
    {
#if DOT_SIMD_SSE
        __m128 mul = _mm_mul_ps(a.simd, b.simd);
        __m128 shuf = _mm_shuffle_ps(mul, mul, _MM_SHUFFLE(2, 3, 0, 1));
        __m128 sums = _mm_add_ps(mul, shuf);
        shuf = _mm_movehl_ps(shuf, sums);
        sums = _mm_add_ss(sums, shuf);
        return _mm_cvtss_f32(sums);
#else
        return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
#endif
    }

    static float Dot3(const Vec4& a, const Vec4& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
    static Vec4 Lerp(const Vec4& a, const Vec4& b, float t) { return a + (b - a) * t; }

    bool ApproxEqual(const Vec4& v, float epsilon = kVec4Epsilon) const
    {
        return std::abs(x - v.x) < epsilon && std::abs(y - v.y) < epsilon && std::abs(z - v.z) < epsilon &&
               std::abs(w - v.w) < epsilon;
    }
};

// Scalar * Vec4
inline Vec4 operator*(float s, const Vec4& v)
{
    return v * s;
}

} // namespace Dot
