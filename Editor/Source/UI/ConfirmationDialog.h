// =============================================================================
// Dot Engine - Reusable Confirmation Dialog
// =============================================================================

#pragma once

#include <array>
#include <functional>
#include <string>

namespace Dot
{

class ConfirmationDialog
{
public:
    struct Button
    {
        std::string label;
        std::function<bool()> onClick;
        bool primary = false;
        bool danger = false;
    };

    void Open(std::string title, std::string message, Button first, Button second, Button third);
    void Draw();
    bool IsOpen() const { return m_IsOpen || m_QueuedOpen; }

private:
    std::string m_Title;
    std::string m_Message;
    std::array<Button, 3> m_Buttons;
    bool m_QueuedOpen = false;
    bool m_IsOpen = false;
};

} // namespace Dot
