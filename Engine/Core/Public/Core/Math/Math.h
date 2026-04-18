// =============================================================================
// Dot Engine - Math Utilities
// =============================================================================
// Common math constants and functions.
// Includes all math types (Vec2, Vec3, Vec4, Mat4, Quat).
// =============================================================================

#pragma once

#include "Core/Core.h"

#include <cmath>
#include <limits>

namespace Dot
{

// =============================================================================
// Constants
// =============================================================================

constexpr float kPi = 3.14159265358979323846f;
constexpr float kTwoPi = 6.28318530717958647692f;
constexpr float kHalfPi = 1.57079632679489661923f;
constexpr float kInvPi = 0.31830988618379067154f;
constexpr float kDegToRad = kPi / 180.0f;
constexpr float kRadToDeg = 180.0f / kPi;
constexpr float kEpsilon = 1e-6f;
constexpr float kInfinity = std::numeric_limits<float>::infinity();

// =============================================================================
// Utility Functions
// =============================================================================

/// Convert degrees to radians
constexpr float Radians(float degrees)
{
    return degrees * kDegToRad;
}

/// Convert radians to degrees
constexpr float Degrees(float radians)
{
    return radians * kRadToDeg;
}

/// Clamp value between min and max
template <typename T> constexpr T Clamp(T value, T minVal, T maxVal)
{
    return value < minVal ? minVal : (value > maxVal ? maxVal : value);
}

/// Linear interpolation
template <typename T> constexpr T Lerp(T a, T b, float t)
{
    return a + static_cast<T>((b - a) * t);
}

/// Check if two floats are approximately equal
inline bool ApproxEqual(float a, float b, float epsilon = kEpsilon)
{
    return std::abs(a - b) < epsilon;
}

/// Square of a value
template <typename T> constexpr T Square(T x)
{
    return x * x;
}

/// Minimum of two values
template <typename T> constexpr T Min(T a, T b)
{
    return a < b ? a : b;
}

/// Maximum of two values
template <typename T> constexpr T Max(T a, T b)
{
    return a > b ? a : b;
}

/// Sign of a value (-1, 0, or 1)
template <typename T> constexpr T Sign(T x)
{
    return x < T(0) ? T(-1) : (x > T(0) ? T(1) : T(0));
}

} // namespace Dot

// =============================================================================
// Math Types - Include AFTER constants/functions are defined!
// =============================================================================
#include "Core/Math/Mat4.h"
#include "Core/Math/Quat.h"
#include "Core/Math/Vec2.h"
#include "Core/Math/Vec3.h"
#include "Core/Math/Vec4.h"

