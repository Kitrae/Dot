#pragma once

#include "Core/Core.h"
#include "Core/Physics/CollisionLayers.h"
#include "Core/Math/Vec3.h"

namespace Dot
{

/// Character Controller Component - kinematic character movement with collision
/// Works with any collider (BoxCollider, SphereCollider)
struct DOT_CORE_API CharacterControllerComponent
{
    // ========== Movement ==========
    float moveSpeed = 5.0f;        // Base movement speed (m/s)
    float sprintMultiplier = 1.5f; // Sprint speed multiplier
    float airControl = 1.0f;       // Movement control while airborne (0-1, 1 = full control)
    float acceleration = 10.0f;    // Ground acceleration
    float deceleration = 10.0f;    // Ground deceleration (friction)
    float airAcceleration = 10.0f; // Air acceleration (matches ground for consistency)

    // ========== Gravity & Jumping ==========
    bool useGravity = true;         // Apply gravity
    float gravityMultiplier = 1.0f; // Gravity scale (1 = normal, 2 = heavy)
    float jumpHeight = 2.0f;        // Jump height in meters
    int maxJumps = 1;               // Total jumps allowed (1 = no double jump)
    float coyoteTime = 0.15f;       // Grace period after leaving ground (seconds)
    float jumpBufferTime = 0.1f;    // Pre-land jump buffering (seconds)

    // ========== Ground Detection ==========
    float groundCheckDistance = 0.1f; // Raycast distance for ground check
    float groundCheckRadius = 0.0f;   // Spherecast radius (0 = raycast)

    // ========== Slopes ==========
    float slopeLimit = 45.0f;             // Max walkable slope angle (degrees)
    bool slideOnSteepSlopes = true;       // Slide down slopes > slopeLimit
    float slideSpeed = 5.0f;              // Slope slide velocity
    bool maintainVelocityOnSlopes = true; // Adjust speed on inclines

    // ========== Stepping ==========
    bool enableStepping = true; // Allow step climbing
    float stepHeight = 0.3f;    // Max step height (meters)
    bool stepSmoothing = true;  // Smooth step transitions

    // ========== Physics Interaction ==========
    bool pushRigidbodies = true; // Push dynamic objects
    float pushForce = 2.0f;      // Force applied to pushed objects
    bool canBePushed = false;    // Can be pushed by other controllers

    // ========== Collision ==========
    float skinWidth = 0.01f;     // Collision margin
    bool slideAlongWalls = true; // Wall sliding behavior
    int maxSlideIterations = 4;  // Collision resolution iterations
    uint8 collisionLayer = 0;    // Project collision layer
    uint32 collisionMask = CollisionLayers::kAllLayersMask; // Which layers this controller interacts with

    // ========== Runtime State (readonly) ==========
    bool isGrounded = false;        // Currently on ground
    bool wasGrounded = false;       // Was grounded last frame
    Vec3 groundNormal = Vec3::Up(); // Ground surface normal
    Vec3 velocity = Vec3::Zero();   // Current velocity
    int jumpsRemaining = 1;         // Jumps left before landing
    float timeSinceGrounded = 0.0f; // Time since last grounded (for coyote time)
    float jumpBufferTimer = 0.0f;   // Jump buffer countdown
    bool isSprinting = false;       // Sprint state
    bool isSliding = false;         // Sliding on steep slope
};

} // namespace Dot
