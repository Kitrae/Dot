// =============================================================================
// Dot Engine - Mat4
// =============================================================================
// 4x4 matrix for transforms. Column-major storage (OpenGL convention).
// =============================================================================

#pragma once

#include "Core/Math/Vec3.h"
#include "Core/Math/Vec4.h"

#include <cmath>

namespace Dot
{

struct Quat; // Forward declaration

/// 4x4 Matrix (column-major)
/// Memory layout: columns[0..3] where each column is a Vec4
struct alignas(16) Mat4
{
    Vec4 columns[4];

    // Constructors
    Mat4()
    {
        columns[0] = Vec4::UnitX();
        columns[1] = Vec4::UnitY();
        columns[2] = Vec4::UnitZ();
        columns[3] = Vec4::UnitW();
    }

    Mat4(float diagonal)
    {
        columns[0] = Vec4(diagonal, 0, 0, 0);
        columns[1] = Vec4(0, diagonal, 0, 0);
        columns[2] = Vec4(0, 0, diagonal, 0);
        columns[3] = Vec4(0, 0, 0, diagonal);
    }

    Mat4(const Vec4& c0, const Vec4& c1, const Vec4& c2, const Vec4& c3)
    {
        columns[0] = c0;
        columns[1] = c1;
        columns[2] = c2;
        columns[3] = c3;
    }

    // Static constructors
    static Mat4 Identity() { return Mat4(1.0f); }
    static Mat4 Zero() { return Mat4(0.0f); }

    static Mat4 Translation(const Vec3& t)
    {
        Mat4 m(1.0f);
        m.columns[3] = Vec4(t, 1.0f);
        return m;
    }

    static Mat4 Translation(float x, float y, float z) { return Translation(Vec3(x, y, z)); }

    static Mat4 Scale(const Vec3& s)
    {
        Mat4 m(1.0f);
        m.columns[0].x = s.x;
        m.columns[1].y = s.y;
        m.columns[2].z = s.z;
        return m;
    }

    static Mat4 Scale(float x, float y, float z) { return Scale(Vec3(x, y, z)); }

    static Mat4 Scale(float uniform) { return Scale(Vec3(uniform, uniform, uniform)); }

    static Mat4 RotationX(float radians)
    {
        float c = std::cos(radians);
        float s = std::sin(radians);
        Mat4 m(1.0f);
        m.columns[1].y = c;
        m.columns[1].z = s;
        m.columns[2].y = -s;
        m.columns[2].z = c;
        return m;
    }

    static Mat4 RotationY(float radians)
    {
        float c = std::cos(radians);
        float s = std::sin(radians);
        Mat4 m(1.0f);
        m.columns[0].x = c;
        m.columns[0].z = -s;
        m.columns[2].x = s;
        m.columns[2].z = c;
        return m;
    }

    static Mat4 RotationZ(float radians)
    {
        float c = std::cos(radians);
        float s = std::sin(radians);
        Mat4 m(1.0f);
        m.columns[0].x = c;
        m.columns[0].y = s;
        m.columns[1].x = -s;
        m.columns[1].y = c;
        return m;
    }

    static Mat4 LookAt(const Vec3& eye, const Vec3& target, const Vec3& up)
    {
        Vec3 f = (target - eye).Normalized();
        Vec3 r = Vec3::Cross(f, up).Normalized();
        Vec3 u = Vec3::Cross(r, f);

        Mat4 m(1.0f);
        m.columns[0] = Vec4(r.x, u.x, -f.x, 0);
        m.columns[1] = Vec4(r.y, u.y, -f.y, 0);
        m.columns[2] = Vec4(r.z, u.z, -f.z, 0);
        m.columns[3] = Vec4(-Vec3::Dot(r, eye), -Vec3::Dot(u, eye), Vec3::Dot(f, eye), 1);
        return m;
    }

    static Mat4 Perspective(float fovY, float aspect, float nearZ, float farZ)
    {
        float tanHalfFov = std::tan(fovY * 0.5f);

        Mat4 m(0.0f);
        m.columns[0].x = 1.0f / (aspect * tanHalfFov);
        m.columns[1].y = 1.0f / tanHalfFov;
        m.columns[2].z = -(farZ + nearZ) / (farZ - nearZ);
        m.columns[2].w = -1.0f;
        m.columns[3].z = -(2.0f * farZ * nearZ) / (farZ - nearZ);
        return m;
    }

    static Mat4 Orthographic(float left, float right, float bottom, float top, float nearZ, float farZ)
    {
        Mat4 m(1.0f);
        m.columns[0].x = 2.0f / (right - left);
        m.columns[1].y = 2.0f / (top - bottom);
        m.columns[2].z = -2.0f / (farZ - nearZ);
        m.columns[3].x = -(right + left) / (right - left);
        m.columns[3].y = -(top + bottom) / (top - bottom);
        m.columns[3].z = -(farZ + nearZ) / (farZ - nearZ);
        return m;
    }

    // Operators
    Vec4& operator[](int i) { return columns[i]; }
    const Vec4& operator[](int i) const { return columns[i]; }

    Mat4 operator*(const Mat4& m) const
    {
        Mat4 result(0.0f);
        for (int c = 0; c < 4; ++c)
        {
            for (int r = 0; r < 4; ++r)
            {
                result.columns[c][r] = columns[0][r] * m.columns[c][0] + columns[1][r] * m.columns[c][1] +
                                       columns[2][r] * m.columns[c][2] + columns[3][r] * m.columns[c][3];
            }
        }
        return result;
    }

    Vec4 operator*(const Vec4& v) const
    {
        return columns[0] * v.x + columns[1] * v.y + columns[2] * v.z + columns[3] * v.w;
    }

    Vec3 TransformPoint(const Vec3& p) const
    {
        Vec4 result = *this * Vec4(p, 1.0f);
        return result.ToVec3();
    }

    Vec3 TransformDirection(const Vec3& d) const
    {
        Vec4 result = *this * Vec4(d, 0.0f);
        return result.ToVec3();
    }

    // Extract components
    Vec3 GetTranslation() const { return columns[3].ToVec3(); }
    Vec3 GetScale() const { return Vec3(columns[0].Length3(), columns[1].Length3(), columns[2].Length3()); }

    Mat4 Transposed() const
    {
        Mat4 result;
        for (int c = 0; c < 4; ++c)
        {
            for (int r = 0; r < 4; ++r)
            {
                result.columns[c][r] = columns[r][c];
            }
        }
        return result;
    }

    float Determinant() const
    {
        float a00 = columns[0][0], a10 = columns[1][0], a20 = columns[2][0], a30 = columns[3][0];
        float a01 = columns[0][1], a11 = columns[1][1], a21 = columns[2][1], a31 = columns[3][1];
        float a02 = columns[0][2], a12 = columns[1][2], a22 = columns[2][2], a32 = columns[3][2];
        float a03 = columns[0][3], a13 = columns[1][3], a23 = columns[2][3], a33 = columns[3][3];

        float kp_lo = a22 * a33 - a32 * a23;
        float jp_ln = a12 * a33 - a32 * a13;
        float jo_kn = a12 * a23 - a22 * a13;
        float ip_lm = a02 * a33 - a32 * a03;
        float io_km = a02 * a23 - a22 * a03;
        float in_jm = a02 * a13 - a12 * a03;

        return a00 * (a11 * kp_lo - a21 * jp_ln + a31 * jo_kn) - a10 * (a01 * kp_lo - a21 * ip_lm + a31 * io_km) +
               a20 * (a01 * jp_ln - a11 * ip_lm + a31 * in_jm) - a30 * (a01 * jo_kn - a11 * io_km + a21 * in_jm);
    }

    Mat4 Inverted() const
    {
        float a00 = columns[0][0], a01 = columns[0][1], a02 = columns[0][2], a03 = columns[0][3];
        float a10 = columns[1][0], a11 = columns[1][1], a12 = columns[1][2], a13 = columns[1][3];
        float a20 = columns[2][0], a21 = columns[2][1], a22 = columns[2][2], a23 = columns[2][3];
        float a30 = columns[3][0], a31 = columns[3][1], a32 = columns[3][2], a33 = columns[3][3];

        float b00 = a00 * a11 - a01 * a10;
        float b01 = a00 * a12 - a02 * a10;
        float b02 = a00 * a13 - a03 * a10;
        float b03 = a01 * a12 - a02 * a11;
        float b04 = a01 * a13 - a03 * a11;
        float b05 = a02 * a13 - a03 * a12;
        float b06 = a20 * a31 - a21 * a30;
        float b07 = a20 * a32 - a22 * a30;
        float b08 = a20 * a33 - a23 * a30;
        float b09 = a21 * a32 - a22 * a31;
        float b10 = a21 * a33 - a23 * a31;
        float b11 = a22 * a33 - a23 * a32;

        float det = b00 * b11 - b01 * b10 + b02 * b09 + b03 * b08 - b04 * b07 + b05 * b06;
        if (std::abs(det) < 1e-12f)
            return Mat4::Identity();

        float invDet = 1.0f / det;
        Mat4 res;
        res.columns[0][0] = (a11 * b11 - a12 * b10 + a13 * b09) * invDet;
        res.columns[0][1] = (-a01 * b11 + a02 * b10 - a03 * b09) * invDet;
        res.columns[0][2] = (a31 * b05 - a32 * b04 + a33 * b03) * invDet;
        res.columns[0][3] = (-a21 * b05 + a22 * b04 - a23 * b03) * invDet;
        res.columns[1][0] = (-a10 * b11 + a12 * b08 - a13 * b07) * invDet;
        res.columns[1][1] = (a00 * b11 - a02 * b08 + a03 * b07) * invDet;
        res.columns[1][2] = (-a30 * b05 + a32 * b02 - a33 * b01) * invDet;
        res.columns[1][3] = (a20 * b05 - a22 * b02 + a23 * b01) * invDet;
        res.columns[2][0] = (a10 * b10 - a11 * b08 + a13 * b06) * invDet;
        res.columns[2][1] = (-a00 * b10 + a01 * b08 - a03 * b06) * invDet;
        res.columns[2][2] = (a30 * b04 - a31 * b02 + a33 * b00) * invDet;
        res.columns[2][3] = (-a20 * b04 + a21 * b02 - a23 * b00) * invDet;
        res.columns[3][0] = (-a10 * b09 + a11 * b07 - a12 * b06) * invDet;
        res.columns[3][1] = (a00 * b09 - a01 * b07 + a02 * b06) * invDet;
        res.columns[3][2] = (-a30 * b03 + a31 * b01 - a32 * b00) * invDet;
        res.columns[3][3] = (a20 * b03 - a21 * b01 + a22 * b00) * invDet;

        return res;
    }

    const float* Data() const { return &columns[0].x; }
    float* Data() { return &columns[0].x; }
};

} // namespace Dot
