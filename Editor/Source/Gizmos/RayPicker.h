// =============================================================================
// Dot Engine - Ray Picker
// =============================================================================
// Generates pick rays from screen coordinates using camera matrices.
// =============================================================================

#pragma once

#include "RayMath.h"

namespace Dot
{

class Camera;

/// Generates pick rays from screen coordinates
class RayPicker
{
public:
    /// Generate a ray from screen coordinates
    /// @param mouseX Screen X coordinate (0 = left)
    /// @param mouseY Screen Y coordinate (0 = top)
    /// @param viewportWidth Width of viewport in pixels
    /// @param viewportHeight Height of viewport in pixels
    /// @param camera Camera to use for unprojection
    static Ray ScreenToWorldRay(float mouseX, float mouseY, float viewportWidth, float viewportHeight,
                                const Camera& camera);
};

} // namespace Dot
