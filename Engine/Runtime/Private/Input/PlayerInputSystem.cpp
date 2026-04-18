// =============================================================================
// Dot Engine - Player Input System Implementation
// =============================================================================

#include "Core/Input/PlayerInputSystem.h"

#include "Core/ECS/World.h"
#include "Core/Input/PlayerInputComponent.h"
#include "Core/Math/Vec3.h"
#include "Core/Physics/CharacterControllerComponent.h"
#include "Core/Physics/CharacterControllerSystem.h"
#include "Core/Scene/Components.h"

#ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <cmath>

namespace Dot
{

namespace
{

bool RuntimeAppHasKeyboardFocus()
{
    HWND foregroundWindow = GetForegroundWindow();
    if (!foregroundWindow)
        return false;

    DWORD foregroundProcessId = 0;
    GetWindowThreadProcessId(foregroundWindow, &foregroundProcessId);
    return foregroundProcessId == GetCurrentProcessId();
}

} // namespace

PlayerInputSystem::PlayerInputSystem() {}
PlayerInputSystem::~PlayerInputSystem() {}

void PlayerInputSystem::SetMouseCaptured(bool captured)
{
    m_MouseCaptured = captured;
    m_FirstMouse = true;
    ApplyCursorVisibility(!m_MouseCaptured || !RuntimeAppHasKeyboardFocus());
}

void PlayerInputSystem::ApplyCursorVisibility(bool visible)
{
    if (m_CursorVisible == visible)
        return;

    if (visible)
    {
        while (ShowCursor(TRUE) < 0)
        {
        }
        SetCursor(LoadCursor(nullptr, IDC_ARROW));
    }
    else
    {
        while (ShowCursor(FALSE) >= 0)
        {
        }
    }

    m_CursorVisible = visible;
}

void PlayerInputSystem::Update(World& world, CharacterControllerSystem& ccSystem, float dt)
{
    static bool tabWasPressed = false;
    const bool appHasFocus = RuntimeAppHasKeyboardFocus();
    ApplyCursorVisibility(!m_MouseCaptured || !appHasFocus);

    if (!appHasFocus)
    {
        tabWasPressed = false;
        m_FirstMouse = true;
        world.Each<TransformComponent, CharacterControllerComponent, PlayerInputComponent>(
            [&](Entity entity, TransformComponent&, CharacterControllerComponent&, PlayerInputComponent&)
            {
                ccSystem.Move(world, entity, Vec3::Zero(), false, false, dt);
            });
        return;
    }

    // Toggle mouse capture with Tab key (Escape closes editor)
    bool tabPressed = (GetAsyncKeyState(VK_TAB) & 0x8000) != 0;
    if (tabPressed && !tabWasPressed)
    {
        SetMouseCaptured(!m_MouseCaptured);
    }
    tabWasPressed = tabPressed;

    world.Each<TransformComponent, CharacterControllerComponent, PlayerInputComponent>(
        [&](Entity entity, TransformComponent& transform, CharacterControllerComponent&, PlayerInputComponent& input)
        {
            // Read key states
            bool forward = (GetAsyncKeyState(input.keyForward) & 0x8000) != 0;
            bool backward = (GetAsyncKeyState(input.keyBackward) & 0x8000) != 0;
            bool left = (GetAsyncKeyState(input.keyLeft) & 0x8000) != 0;
            bool right = (GetAsyncKeyState(input.keyRight) & 0x8000) != 0;
            bool jump = (GetAsyncKeyState(input.keyJump) & 0x8000) != 0;
            bool sprint = (GetAsyncKeyState(input.keySprint) & 0x8000) != 0;

            // Calculate movement direction in world space
            Vec3 moveDir = Vec3::Zero();

            // Get forward/right vectors from entity rotation
            float yawRad = transform.rotation.y * 0.0174532925f; // deg to rad
            Vec3 forwardDir = Vec3(std::sin(yawRad), 0.0f, std::cos(yawRad));
            Vec3 rightDir = Vec3(std::cos(yawRad), 0.0f, -std::sin(yawRad));

            if (forward)
                moveDir = moveDir + forwardDir;
            if (backward)
                moveDir = moveDir - forwardDir;
            if (right)
                moveDir = moveDir + rightDir;
            if (left)
                moveDir = moveDir - rightDir;

            // Normalize movement
            if (moveDir.LengthSquared() > 0.001f)
            {
                moveDir = moveDir.Normalized();
            }

            // Handle mouse look
            if (m_MouseCaptured && input.enableMouseLook)
            {
                POINT cursorPos;
                GetCursorPos(&cursorPos);

                if (m_FirstMouse)
                {
                    m_LastMouseX = cursorPos.x;
                    m_LastMouseY = cursorPos.y;
                    m_FirstMouse = false;
                }

                int deltaX = cursorPos.x - m_LastMouseX;
                int deltaY = cursorPos.y - m_LastMouseY;

                input.lookYaw += static_cast<float>(deltaX) * input.mouseSensitivity;
                input.lookPitch += static_cast<float>(input.invertY ? -deltaY : deltaY) * input.mouseSensitivity;

                // Clamp pitch
                if (input.lookPitch > input.pitchMax)
                    input.lookPitch = input.pitchMax;
                if (input.lookPitch < input.pitchMin)
                    input.lookPitch = input.pitchMin;

                // Wrap yaw
                while (input.lookYaw > 360.0f)
                    input.lookYaw -= 360.0f;
                while (input.lookYaw < 0.0f)
                    input.lookYaw += 360.0f;

                // Apply rotation to entity
                transform.rotation.y = input.lookYaw;
                transform.rotation.x = input.lookPitch;

                // Reset cursor to center of screen
                HWND hwnd = GetActiveWindow();
                if (hwnd)
                {
                    RECT rect;
                    GetClientRect(hwnd, &rect);
                    POINT center = {(rect.right - rect.left) / 2, (rect.bottom - rect.top) / 2};
                    ClientToScreen(hwnd, &center);
                    SetCursorPos(center.x, center.y);
                    m_LastMouseX = center.x;
                    m_LastMouseY = center.y;
                }
            }

            // Call character controller
            ccSystem.Move(world, entity, moveDir, sprint, jump, dt);
        });
}

} // namespace Dot
