// =============================================================================
// Common - Math Utilities
// =============================================================================
// Shared math functions and constants for all shaders.
// =============================================================================

#ifndef MATH_HLSLI
#define MATH_HLSLI

static const float PI = 3.14159265359f;
static const float TWO_PI = 6.28318530718f;
static const float HALF_PI = 1.57079632679f;
static const float INV_PI = 0.31830988618f;

// Linearize a depth value from [0,1] to view-space distance
float LinearizeDepth(float d, float nearPlane, float farPlane)
{
    return nearPlane * farPlane / (farPlane - d * (farPlane - nearPlane));
}

// Default near/far for convenience
float LinearizeDepth(float d)
{
    return LinearizeDepth(d, 0.1f, 1000.0f);
}

// Interleaved gradient noise for per-pixel randomization
float InterleavedGradientNoise(float2 screenPos)
{
    return frac(52.9829189f * frac(0.06711056f * screenPos.x + 0.00583715f * screenPos.y));
}

// Fast approximate atan2
float FastAtan2(float y, float x)
{
    return atan2(y, x);
}

#endif // MATH_HLSLI
