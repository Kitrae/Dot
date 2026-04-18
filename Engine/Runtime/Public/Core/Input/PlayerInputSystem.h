#pragma once

#include "Core/Core.h"
#include "Core/ECS/Entity.h"

namespace Dot
{

class World;

/// Player Input System - processes input and feeds to CharacterController
class DOT_CORE_API PlayerInputSystem
{
public:
    PlayerInputSystem();
    ~PlayerInputSystem();

    /// Update all entities with PlayerInputComponent
    /// Reads keyboard/mouse state and calls CharacterControllerSystem::Move()
    void Update(World& world, class CharacterControllerSystem& ccSystem, float dt);

    /// Set mouse captured state (for FPS-style mouse look)
    void SetMouseCaptured(bool captured);
    bool IsMouseCaptured() const { return m_MouseCaptured; }

private:
    void ApplyCursorVisibility(bool visible);

    bool m_MouseCaptured = false;
    int m_LastMouseX = 0;
    int m_LastMouseY = 0;
    bool m_FirstMouse = true;
    bool m_CursorVisible = true;
};

} // namespace Dot
