// =============================================================================
// Dot Engine - Math Unit Tests
// =============================================================================

#include "Core/Math/Mat4.h"
#include "Core/Math/Math.h"
#include "Core/Math/Quat.h"
#include "Core/Math/Vec2.h"
#include "Core/Math/Vec3.h"
#include "Core/Math/Vec4.h"

#include <gtest/gtest.h>


using namespace Dot;

// =============================================================================
// Math Utilities Tests
// =============================================================================

TEST(MathTests, Constants)
{
    EXPECT_FLOAT_EQ(kPi, 3.14159265358979323846f);
    EXPECT_FLOAT_EQ(kTwoPi, kPi * 2.0f);
    EXPECT_FLOAT_EQ(kHalfPi, kPi / 2.0f);
}

TEST(MathTests, RadiansDegrees)
{
    EXPECT_NEAR(Radians(180.0f), kPi, kEpsilon);
    EXPECT_NEAR(Degrees(kPi), 180.0f, kEpsilon);
    EXPECT_NEAR(Radians(90.0f), kHalfPi, kEpsilon);
}

TEST(MathTests, Clamp)
{
    EXPECT_EQ(Clamp(5, 0, 10), 5);
    EXPECT_EQ(Clamp(-5, 0, 10), 0);
    EXPECT_EQ(Clamp(15, 0, 10), 10);
}

TEST(MathTests, Lerp)
{
    EXPECT_FLOAT_EQ(Lerp(0.0f, 10.0f, 0.5f), 5.0f);
    EXPECT_FLOAT_EQ(Lerp(0.0f, 10.0f, 0.0f), 0.0f);
    EXPECT_FLOAT_EQ(Lerp(0.0f, 10.0f, 1.0f), 10.0f);
}

// =============================================================================
// Vec2 Tests
// =============================================================================

TEST(Vec2Tests, Construction)
{
    Vec2 v1;
    EXPECT_FLOAT_EQ(v1.x, 0.0f);
    EXPECT_FLOAT_EQ(v1.y, 0.0f);

    Vec2 v2(3.0f, 4.0f);
    EXPECT_FLOAT_EQ(v2.x, 3.0f);
    EXPECT_FLOAT_EQ(v2.y, 4.0f);
}

TEST(Vec2Tests, Arithmetic)
{
    Vec2 a(1, 2);
    Vec2 b(3, 4);

    Vec2 sum = a + b;
    EXPECT_FLOAT_EQ(sum.x, 4.0f);
    EXPECT_FLOAT_EQ(sum.y, 6.0f);

    Vec2 diff = b - a;
    EXPECT_FLOAT_EQ(diff.x, 2.0f);
    EXPECT_FLOAT_EQ(diff.y, 2.0f);
}

TEST(Vec2Tests, Length)
{
    Vec2 v(3.0f, 4.0f);
    EXPECT_FLOAT_EQ(v.Length(), 5.0f);
    EXPECT_FLOAT_EQ(v.LengthSquared(), 25.0f);
}

TEST(Vec2Tests, Normalize)
{
    Vec2 v(3.0f, 4.0f);
    Vec2 n = v.Normalized();
    EXPECT_NEAR(n.Length(), 1.0f, kEpsilon);
}

TEST(Vec2Tests, Dot)
{
    Vec2 a(1, 0);
    Vec2 b(0, 1);
    EXPECT_FLOAT_EQ(Vec2::Dot(a, b), 0.0f); // Perpendicular

    Vec2 c(1, 0);
    EXPECT_FLOAT_EQ(Vec2::Dot(a, c), 1.0f); // Parallel
}

// =============================================================================
// Vec3 Tests
// =============================================================================

TEST(Vec3Tests, Construction)
{
    Vec3 v(1, 2, 3);
    EXPECT_FLOAT_EQ(v.x, 1.0f);
    EXPECT_FLOAT_EQ(v.y, 2.0f);
    EXPECT_FLOAT_EQ(v.z, 3.0f);
}

TEST(Vec3Tests, Cross)
{
    Vec3 x = Vec3::UnitX();
    Vec3 y = Vec3::UnitY();
    Vec3 z = Vec3::Cross(x, y);

    EXPECT_NEAR(z.x, 0.0f, kEpsilon);
    EXPECT_NEAR(z.y, 0.0f, kEpsilon);
    EXPECT_NEAR(z.z, 1.0f, kEpsilon);
}

TEST(Vec3Tests, Normalize)
{
    Vec3 v(1, 2, 3);
    Vec3 n = v.Normalized();
    EXPECT_NEAR(n.Length(), 1.0f, kEpsilon);
}

// =============================================================================
// Vec4 Tests
// =============================================================================

TEST(Vec4Tests, Construction)
{
    Vec4 v(1, 2, 3, 4);
    EXPECT_FLOAT_EQ(v.x, 1.0f);
    EXPECT_FLOAT_EQ(v.y, 2.0f);
    EXPECT_FLOAT_EQ(v.z, 3.0f);
    EXPECT_FLOAT_EQ(v.w, 4.0f);
}

TEST(Vec4Tests, FromVec3)
{
    Vec3 v3(1, 2, 3);
    Vec4 v4(v3, 1.0f);
    EXPECT_FLOAT_EQ(v4.x, 1.0f);
    EXPECT_FLOAT_EQ(v4.y, 2.0f);
    EXPECT_FLOAT_EQ(v4.z, 3.0f);
    EXPECT_FLOAT_EQ(v4.w, 1.0f);
}

TEST(Vec4Tests, Arithmetic)
{
    Vec4 a(1, 2, 3, 4);
    Vec4 b(5, 6, 7, 8);

    Vec4 sum = a + b;
    EXPECT_FLOAT_EQ(sum.x, 6.0f);
    EXPECT_FLOAT_EQ(sum.y, 8.0f);
    EXPECT_FLOAT_EQ(sum.z, 10.0f);
    EXPECT_FLOAT_EQ(sum.w, 12.0f);
}

TEST(Vec4Tests, Dot)
{
    Vec4 a(1, 0, 0, 0);
    Vec4 b(0, 1, 0, 0);
    EXPECT_FLOAT_EQ(Vec4::Dot(a, b), 0.0f);

    EXPECT_FLOAT_EQ(Vec4::Dot(a, a), 1.0f);
}

// =============================================================================
// Mat4 Tests
// =============================================================================

TEST(Mat4Tests, Identity)
{
    Mat4 m = Mat4::Identity();
    EXPECT_FLOAT_EQ(m[0][0], 1.0f);
    EXPECT_FLOAT_EQ(m[1][1], 1.0f);
    EXPECT_FLOAT_EQ(m[2][2], 1.0f);
    EXPECT_FLOAT_EQ(m[3][3], 1.0f);
    EXPECT_FLOAT_EQ(m[0][1], 0.0f);
}

TEST(Mat4Tests, Translation)
{
    Mat4 t = Mat4::Translation(1, 2, 3);
    Vec3 p = t.TransformPoint(Vec3::Zero());
    EXPECT_NEAR(p.x, 1.0f, kEpsilon);
    EXPECT_NEAR(p.y, 2.0f, kEpsilon);
    EXPECT_NEAR(p.z, 3.0f, kEpsilon);
}

TEST(Mat4Tests, Scale)
{
    Mat4 s = Mat4::Scale(2, 3, 4);
    Vec3 p = s.TransformPoint(Vec3(1, 1, 1));
    EXPECT_NEAR(p.x, 2.0f, kEpsilon);
    EXPECT_NEAR(p.y, 3.0f, kEpsilon);
    EXPECT_NEAR(p.z, 4.0f, kEpsilon);
}

TEST(Mat4Tests, Multiplication)
{
    Mat4 a = Mat4::Translation(1, 0, 0);
    Mat4 b = Mat4::Translation(0, 1, 0);
    Mat4 c = a * b;

    Vec3 p = c.TransformPoint(Vec3::Zero());
    EXPECT_NEAR(p.x, 1.0f, kEpsilon);
    EXPECT_NEAR(p.y, 1.0f, kEpsilon);
}

// =============================================================================
// Quaternion Tests
// =============================================================================

TEST(QuatTests, Identity)
{
    Quat q = Quat::Identity();
    EXPECT_FLOAT_EQ(q.x, 0.0f);
    EXPECT_FLOAT_EQ(q.y, 0.0f);
    EXPECT_FLOAT_EQ(q.z, 0.0f);
    EXPECT_FLOAT_EQ(q.w, 1.0f);
}

TEST(QuatTests, FromAxisAngle)
{
    Quat q = Quat::FromAxisAngle(Vec3::UnitY(), kHalfPi); // 90 degrees around Y
    Vec3 v = q * Vec3::UnitX();                           // Rotate X axis

    EXPECT_NEAR(v.x, 0.0f, kEpsilon);
    EXPECT_NEAR(v.y, 0.0f, kEpsilon);
    EXPECT_NEAR(v.z, -1.0f, kEpsilon); // Should point -Z
}

TEST(QuatTests, Multiplication)
{
    Quat a = Quat::FromAxisAngle(Vec3::UnitY(), kHalfPi);
    Quat b = Quat::FromAxisAngle(Vec3::UnitY(), kHalfPi);
    Quat c = a * b; // 180 degrees total

    Vec3 v = c * Vec3::UnitX();
    EXPECT_NEAR(v.x, -1.0f, kEpsilon);
    EXPECT_NEAR(v.y, 0.0f, kEpsilon);
    EXPECT_NEAR(v.z, 0.0f, kEpsilon);
}

TEST(QuatTests, Slerp)
{
    Quat a = Quat::Identity();
    Quat b = Quat::FromAxisAngle(Vec3::UnitY(), kPi); // 180 degrees

    Quat mid = Quat::Slerp(a, b, 0.5f);
    Vec3 v = mid * Vec3::UnitX();

    // At 90 degrees, X should be at Z
    EXPECT_NEAR(std::abs(v.z), 1.0f, 0.01f);
}

TEST(QuatTests, Normalize)
{
    Quat q(1, 2, 3, 4);
    Quat n = q.Normalized();
    EXPECT_NEAR(n.Length(), 1.0f, kEpsilon);
}
