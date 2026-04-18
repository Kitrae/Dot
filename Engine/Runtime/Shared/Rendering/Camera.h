// =============================================================================
// Dot Engine - Simple Camera
// =============================================================================
// Basic 3D camera with view and projection matrices.
// =============================================================================

#pragma once

#include <cmath>

namespace Dot
{

/// Simple 3D camera for viewport rendering
class Camera
{
public:
    Camera() { UpdateMatrices(); }

    // Position
    void SetPosition(float x, float y, float z)
    {
        m_PosX = x;
        m_PosY = y;
        m_PosZ = z;
        UpdateMatrices();
    }

    // Look at target
    void LookAt(float targetX, float targetY, float targetZ)
    {
        m_TargetX = targetX;
        m_TargetY = targetY;
        m_TargetZ = targetZ;
        m_UpX = 0.0f;
        m_UpY = 1.0f;
        m_UpZ = 0.0f;
        UpdateMatrices();
    }

    void LookAtWithUp(float targetX, float targetY, float targetZ, float upX, float upY, float upZ)
    {
        m_TargetX = targetX;
        m_TargetY = targetY;
        m_TargetZ = targetZ;
        m_UpX = upX;
        m_UpY = upY;
        m_UpZ = upZ;
        UpdateMatrices();
    }

    // Projection
    void SetPerspective(float fovDegrees, float aspectRatio, float nearZ, float farZ)
    {
        m_FOV = fovDegrees * 3.14159265f / 180.0f;
        m_Aspect = aspectRatio;
        m_NearZ = nearZ;
        m_FarZ = farZ;
        UpdateMatrices();
    }

    void SetAspectRatio(float aspect)
    {
        m_Aspect = aspect;
        UpdateMatrices();
    }

    // Get matrices (column-major for HLSL mul(MVP, pos))
    const float* GetViewMatrix() const { return m_View; }
    const float* GetProjectionMatrix() const { return m_Projection; }
    const float* GetViewProjectionMatrix() const { return m_ViewProjection; }

    // Get camera parameters for ray picking
    void GetPosition(float& x, float& y, float& z) const
    {
        x = m_PosX;
        y = m_PosY;
        z = m_PosZ;
    }
    void GetTarget(float& x, float& y, float& z) const
    {
        x = m_TargetX;
        y = m_TargetY;
        z = m_TargetZ;
    }
    float GetFOV() const { return m_FOV; }
    float GetAspect() const { return m_Aspect; }
    float GetNearZ() const { return m_NearZ; }
    float GetFarZ() const { return m_FarZ; }
    float GetFOVDegrees() const { return m_FOV * 180.0f / 3.14159265f; }

    // Position components for scripting
    float GetPosX() const { return m_PosX; }
    float GetPosY() const { return m_PosY; }
    float GetPosZ() const { return m_PosZ; }

    // Direction vectors extracted from view matrix for scripting
    void GetForward(float& x, float& y, float& z) const
    {
        // Forward is column 2 of view matrix
        x = m_View[2];
        y = m_View[6];
        z = m_View[10];
    }
    void GetUp(float& x, float& y, float& z) const
    {
        // Up is column 1 of view matrix
        x = m_View[1];
        y = m_View[5];
        z = m_View[9];
    }
    void GetRight(float& x, float& y, float& z) const
    {
        // Right is column 0 of view matrix
        x = m_View[0];
        y = m_View[4];
        z = m_View[8];
    }

    // =========================================================================
    // Editor Camera Controls
    // =========================================================================

    /// Orbit around the target point (spherical coordinates)
    void Orbit(float deltaYaw, float deltaPitch)
    {
        // Get current position relative to target
        float dx = m_PosX - m_TargetX;
        float dy = m_PosY - m_TargetY;
        float dz = m_PosZ - m_TargetZ;

        // Convert to spherical coordinates
        float distance = std::sqrt(dx * dx + dy * dy + dz * dz);
        float yaw = std::atan2(dx, dz);         // Horizontal angle
        float pitch = std::asin(dy / distance); // Vertical angle

        // Apply rotation
        yaw += deltaYaw;
        pitch += deltaPitch;

        // Clamp pitch to avoid gimbal lock
        const float maxPitch = 1.5f; // ~85 degrees
        if (pitch > maxPitch)
            pitch = maxPitch;
        if (pitch < -maxPitch)
            pitch = -maxPitch;

        // Convert back to Cartesian
        m_PosX = m_TargetX + distance * std::cos(pitch) * std::sin(yaw);
        m_PosY = m_TargetY + distance * std::sin(pitch);
        m_PosZ = m_TargetZ + distance * std::cos(pitch) * std::cos(yaw);

        UpdateMatrices();
    }

    /// Pan the camera (move both position and target)
    void Pan(float deltaRight, float deltaUp)
    {
        // Get camera right and up vectors from view matrix
        float rx = m_View[0], ry = m_View[4], rz = m_View[8];
        float ux = m_View[1], uy = m_View[5], uz = m_View[9];

        // Move camera and target
        m_PosX += rx * deltaRight + ux * deltaUp;
        m_PosY += ry * deltaRight + uy * deltaUp;
        m_PosZ += rz * deltaRight + uz * deltaUp;

        m_TargetX += rx * deltaRight + ux * deltaUp;
        m_TargetY += ry * deltaRight + uy * deltaUp;
        m_TargetZ += rz * deltaRight + uz * deltaUp;

        UpdateMatrices();
    }

    /// Zoom (move camera closer/farther from target)
    void Zoom(float delta)
    {
        float dx = m_PosX - m_TargetX;
        float dy = m_PosY - m_TargetY;
        float dz = m_PosZ - m_TargetZ;
        float distance = std::sqrt(dx * dx + dy * dy + dz * dz);

        // Apply zoom
        distance -= delta;
        if (distance < 0.5f)
            distance = 0.5f; // Minimum distance
        if (distance > 100.0f)
            distance = 100.0f; // Maximum distance

        // Recalculate position
        float nx = dx / std::sqrt(dx * dx + dy * dy + dz * dz);
        float ny = dy / std::sqrt(dx * dx + dy * dy + dz * dz);
        float nz = dz / std::sqrt(dx * dx + dy * dy + dz * dz);

        m_PosX = m_TargetX + nx * distance;
        m_PosY = m_TargetY + ny * distance;
        m_PosZ = m_TargetZ + nz * distance;

        UpdateMatrices();
    }

    /// Focus on a point (set target and adjust position)
    void Focus(float targetX, float targetY, float targetZ, float distance = 5.0f)
    {
        // Keep current viewing direction
        float dx = m_PosX - m_TargetX;
        float dy = m_PosY - m_TargetY;
        float dz = m_PosZ - m_TargetZ;
        float len = std::sqrt(dx * dx + dy * dy + dz * dz);
        if (len < 0.001f)
        {
            dx = 0;
            dy = 0;
            dz = 1;
            len = 1;
        }

        m_TargetX = targetX;
        m_TargetY = targetY;
        m_TargetZ = targetZ;
        m_PosX = targetX + (dx / len) * distance;
        m_PosY = targetY + (dy / len) * distance;
        m_PosZ = targetZ + (dz / len) * distance;

        UpdateMatrices();
    }

    // =========================================================================
    // Frustum Culling
    // =========================================================================

    /// A plane defined by normal (a,b,c) and distance d: ax + by + cz + d = 0
    struct Plane
    {
        float a, b, c, d;

        /// Normalize the plane
        void Normalize()
        {
            float len = std::sqrt(a * a + b * b + c * c);
            if (len > 0.0001f)
            {
                a /= len;
                b /= len;
                c /= len;
                d /= len;
            }
        }

        /// Signed distance to point (positive = in front of plane)
        float DistanceToPoint(float x, float y, float z) const { return a * x + b * y + c * z + d; }
    };

    /// View frustum with 6 planes
    struct Frustum
    {
        Plane planes[6]; // Left, Right, Bottom, Top, Near, Far
        bool useCameraSpaceTest = false;
        float originX = 0.0f, originY = 0.0f, originZ = 0.0f;
        float rightX = 1.0f, rightY = 0.0f, rightZ = 0.0f;
        float upX = 0.0f, upY = 1.0f, upZ = 0.0f;
        float forwardX = 0.0f, forwardY = 0.0f, forwardZ = 1.0f;
        float nearZ = 0.1f;
        float farZ = 100.0f;
        float tanHalfHorizontalFov = 1.0f;
        float tanHalfVerticalFov = 1.0f;

        /// Test if AABB is inside or intersects the frustum
        /// Returns true if the box should be rendered
        bool TestAABB(float minX, float minY, float minZ, float maxX, float maxY, float maxZ) const
        {
            const float centerX = (minX + maxX) * 0.5f;
            const float centerY = (minY + maxY) * 0.5f;
            const float centerZ = (minZ + maxZ) * 0.5f;
            const float extentX = (maxX - minX) * 0.5f;
            const float extentY = (maxY - minY) * 0.5f;
            const float extentZ = (maxZ - minZ) * 0.5f;

            if (useCameraSpaceTest)
            {
                constexpr float kDepthSlack = 0.01f;

                const float relX = centerX - originX;
                const float relY = centerY - originY;
                const float relZ = centerZ - originZ;

                const float camX = relX * rightX + relY * rightY + relZ * rightZ;
                const float camY = relX * upX + relY * upY + relZ * upZ;
                const float camZ = relX * forwardX + relY * forwardY + relZ * forwardZ;

                const float axisExtentX =
                    std::abs(rightX) * extentX + std::abs(rightY) * extentY + std::abs(rightZ) * extentZ;
                const float axisExtentY =
                    std::abs(upX) * extentX + std::abs(upY) * extentY + std::abs(upZ) * extentZ;
                const float axisExtentZ =
                    std::abs(forwardX) * extentX + std::abs(forwardY) * extentY + std::abs(forwardZ) * extentZ;
                constexpr float kSideSlack = 0.01f;

                if (camZ + axisExtentZ < nearZ - kDepthSlack)
                    return false;
                if (camZ - axisExtentZ > farZ + kDepthSlack)
                    return false;

                const float leftDistance = camX + camZ * tanHalfHorizontalFov;
                const float rightDistance = -camX + camZ * tanHalfHorizontalFov;
                const float horizontalRadius = axisExtentX + tanHalfHorizontalFov * axisExtentZ;
                if (leftDistance < -(horizontalRadius + kSideSlack))
                    return false;
                if (rightDistance < -(horizontalRadius + kSideSlack))
                    return false;

                const float bottomDistance = camY + camZ * tanHalfVerticalFov;
                const float topDistance = -camY + camZ * tanHalfVerticalFov;
                const float verticalRadius = axisExtentY + tanHalfVerticalFov * axisExtentZ;
                if (bottomDistance < -(verticalRadius + kSideSlack))
                    return false;
                if (topDistance < -(verticalRadius + kSideSlack))
                    return false;

                return true;
            }

            constexpr float kCullSlack = 0.05f;

            for (int i = 0; i < 6; ++i)
            {
                const Plane& p = planes[i];
                const float distance = p.DistanceToPoint(centerX, centerY, centerZ);
                const float radius =
                    std::abs(p.a) * extentX + std::abs(p.b) * extentY + std::abs(p.c) * extentZ;

                if (distance < -(radius + kCullSlack))
                    return false;
            }
            return true;
        }
    };

    /// Extract frustum planes from the view-projection matrix
    /// Builds the frustum geometrically from camera pose + projection params.
    /// This avoids row/column-major ambiguity when extracting from matrices.
    Frustum GetFrustum() const
    {
        Frustum f;
        auto normalize3 = [](float& x, float& y, float& z)
        {
            const float len = std::sqrt(x * x + y * y + z * z);
            if (len > 0.000001f)
            {
                x /= len;
                y /= len;
                z /= len;
            }
        };

        auto cross = [](float ax, float ay, float az, float bx, float by, float bz, float& outX, float& outY, float& outZ)
        {
            outX = ay * bz - az * by;
            outY = az * bx - ax * bz;
            outZ = ax * by - ay * bx;
        };

        const float eyeX = m_PosX;
        const float eyeY = m_PosY;
        const float eyeZ = m_PosZ;

        float forwardX = m_TargetX - m_PosX;
        float forwardY = m_TargetY - m_PosY;
        float forwardZ = m_TargetZ - m_PosZ;
        normalize3(forwardX, forwardY, forwardZ);

        if (std::abs(forwardX) < 0.000001f && std::abs(forwardY) < 0.000001f && std::abs(forwardZ) < 0.000001f)
        {
            forwardZ = 1.0f;
        }

        float rightX, rightY, rightZ;
        cross(m_UpX, m_UpY, m_UpZ, forwardX, forwardY, forwardZ, rightX, rightY, rightZ);
        normalize3(rightX, rightY, rightZ);

        if (std::abs(rightX) < 0.000001f && std::abs(rightY) < 0.000001f && std::abs(rightZ) < 0.000001f)
        {
            rightX = 1.0f;
            rightY = 0.0f;
            rightZ = 0.0f;
        }

        float upX, upY, upZ;
        cross(forwardX, forwardY, forwardZ, rightX, rightY, rightZ, upX, upY, upZ);
        normalize3(upX, upY, upZ);

        const float verticalHalfAngle = m_FOV * 0.5f;
        f.useCameraSpaceTest = true;
        f.originX = eyeX;
        f.originY = eyeY;
        f.originZ = eyeZ;
        f.rightX = rightX;
        f.rightY = rightY;
        f.rightZ = rightZ;
        f.upX = upX;
        f.upY = upY;
        f.upZ = upZ;
        f.forwardX = forwardX;
        f.forwardY = forwardY;
        f.forwardZ = forwardZ;
        f.nearZ = m_NearZ;
        f.farZ = m_FarZ;
        f.tanHalfVerticalFov = std::tan(verticalHalfAngle);
        f.tanHalfHorizontalFov = f.tanHalfVerticalFov * m_Aspect;

        return f;
    }

private:
    void UpdateMatrices()
    {
        // =====================================================
        // View Matrix (LookAtLH - column-major for mul(MVP, pos))
        // =====================================================
        float eyeX = m_PosX, eyeY = m_PosY, eyeZ = m_PosZ;
        float atX = m_TargetX, atY = m_TargetY, atZ = m_TargetZ;

        // Forward = normalize(at - eye)
        float fx = atX - eyeX, fy = atY - eyeY, fz = atZ - eyeZ;
        float fLen = std::sqrt(fx * fx + fy * fy + fz * fz);
        fx /= fLen;
        fy /= fLen;
        fz /= fLen;

        // Right = normalize(cross(up, forward)) for LH
        float upX = m_UpX, upY = m_UpY, upZ = m_UpZ;
        float rx = upY * fz - upZ * fy;
        float ry = upZ * fx - upX * fz;
        float rz = upX * fy - upY * fx;
        float rLen = std::sqrt(rx * rx + ry * ry + rz * rz);
        if (rLen <= 0.000001f)
        {
            upX = 0.0f;
            upY = std::abs(fy) > 0.99f ? 0.0f : 1.0f;
            upZ = std::abs(fy) > 0.99f ? 1.0f : 0.0f;
            rx = upY * fz - upZ * fy;
            ry = upZ * fx - upX * fz;
            rz = upX * fy - upY * fx;
            rLen = std::sqrt(rx * rx + ry * ry + rz * rz);
        }
        rx /= rLen;
        ry /= rLen;
        rz /= rLen;

        // Up = cross(forward, right) for LH
        float ux = fy * rz - fz * ry;
        float uy = fz * rx - fx * rz;
        float uz = fx * ry - fy * rx;

        // Column-major view matrix: m[col*4 + row]
        // Column 0 = right, Column 1 = up, Column 2 = forward, Column 3 = translation
        m_View[0] = rx;
        m_View[4] = ry;
        m_View[8] = rz;
        m_View[12] = -(rx * eyeX + ry * eyeY + rz * eyeZ);
        m_View[1] = ux;
        m_View[5] = uy;
        m_View[9] = uz;
        m_View[13] = -(ux * eyeX + uy * eyeY + uz * eyeZ);
        m_View[2] = fx;
        m_View[6] = fy;
        m_View[10] = fz;
        m_View[14] = -(fx * eyeX + fy * eyeY + fz * eyeZ);
        m_View[3] = 0;
        m_View[7] = 0;
        m_View[11] = 0;
        m_View[15] = 1;

        // =====================================================
        // Perspective (PerspectiveFovLH - column-major)
        // =====================================================
        float h = 1.0f / std::tan(m_FOV * 0.5f); // cot(fov/2)
        float w = h / m_Aspect;
        float zn = m_NearZ;
        float zf = m_FarZ;

        // Column-major: m[col*4 + row]
        // Col 0: [w, 0, 0, 0]  Col 1: [0, h, 0, 0]  Col 2: [0, 0, zf/(zf-zn), 1]  Col 3: [0, 0, -zn*zf/(zf-zn), 0]
        m_Projection[0] = w;
        m_Projection[4] = 0;
        m_Projection[8] = 0;
        m_Projection[12] = 0;
        m_Projection[1] = 0;
        m_Projection[5] = h;
        m_Projection[9] = 0;
        m_Projection[13] = 0;
        m_Projection[2] = 0;
        m_Projection[6] = 0;
        m_Projection[10] = zf / (zf - zn);
        m_Projection[14] = -zn * zf / (zf - zn);
        m_Projection[3] = 0;
        m_Projection[7] = 0;
        m_Projection[11] = 1;
        m_Projection[15] = 0;

        // ViewProjection = Projection * View (P applied after V in column-major)
        MultiplyMatricesColMajor(m_Projection, m_View, m_ViewProjection);
    }

    void MultiplyMatricesColMajor(const float* a, const float* b, float* out)
    {
        // Column-major: out = a * b (b applied first, then a)
        for (int col = 0; col < 4; col++)
        {
            for (int row = 0; row < 4; row++)
            {
                out[col * 4 + row] = a[0 * 4 + row] * b[col * 4 + 0] + a[1 * 4 + row] * b[col * 4 + 1] +
                                     a[2 * 4 + row] * b[col * 4 + 2] + a[3 * 4 + row] * b[col * 4 + 3];
            }
        }
    }

    // Camera parameters - start behind the origin looking at it
    float m_PosX = 0, m_PosY = 1, m_PosZ = -3;
    float m_TargetX = 0, m_TargetY = 0, m_TargetZ = 0;
    float m_UpX = 0, m_UpY = 1, m_UpZ = 0;
    float m_FOV = 60.0f * 3.14159265f / 180.0f;
    float m_Aspect = 16.0f / 9.0f;
    float m_NearZ = 0.1f;
    float m_FarZ = 100.0f;

    // Matrices (column-major for HLSL with mul(MVP, pos))
    float m_View[16] = {};
    float m_Projection[16] = {};
    float m_ViewProjection[16] = {};
};

} // namespace Dot
