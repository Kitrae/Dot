#pragma once

#include "Core/Core.h"

namespace Dot
{

/// Player Input Component - configurable key bindings for character control
/// Attach to an entity with CharacterControllerComponent to enable WASD movement
struct DOT_CORE_API PlayerInputComponent
{
    // ========== Movement Keys (Virtual Key Codes) ==========
    int keyForward = 'W';  // VK code for forward
    int keyBackward = 'S'; // VK code for backward
    int keyLeft = 'A';     // VK code for left
    int keyRight = 'D';    // VK code for right
    int keyJump = 0x20;    // VK_SPACE
    int keySprint = 0x10;  // VK_SHIFT

    // ========== Look Controls ==========
    bool enableMouseLook = true;    // Use mouse for camera rotation
    float mouseSensitivity = 0.15f; // Mouse look sensitivity
    bool invertY = false;           // Invert Y axis

    // ========== Controller Support (Future) ==========
    bool useController = false; // Use gamepad input
    int controllerIndex = 0;    // Which controller
    float deadzone = 0.15f;     // Analog stick deadzone

    // ========== Runtime State ==========
    float lookYaw = 0.0f;    // Current yaw rotation (degrees)
    float lookPitch = 0.0f;  // Current pitch rotation (degrees)
    float pitchMin = -89.0f; // Pitch clamp minimum
    float pitchMax = 89.0f;  // Pitch clamp maximum

    // Helper: Get key name for display
    static const char* GetKeyName(int vkCode)
    {
        switch (vkCode)
        {
            case 'W':
                return "W";
            case 'A':
                return "A";
            case 'S':
                return "S";
            case 'D':
                return "D";
            case 0x20:
                return "Space";
            case 0x10:
                return "Shift";
            case 0x11:
                return "Ctrl";
            case 0x12:
                return "Alt";
            case 0x1B:
                return "Escape";
            case 0x0D:
                return "Enter";
            case 0x09:
                return "Tab";
            case 0x26:
                return "Up";
            case 0x28:
                return "Down";
            case 0x25:
                return "Left";
            case 0x27:
                return "Right";
            default:
                if (vkCode >= 'A' && vkCode <= 'Z')
                {
                    static char buf[2] = {0, 0};
                    buf[0] = static_cast<char>(vkCode);
                    return buf;
                }
                return "?";
        }
    }
};

} // namespace Dot
