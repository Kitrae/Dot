// =============================================================================
// Dot Engine - Console Panel
// =============================================================================
// Displays engine log messages in the editor with filtering and colors.
// =============================================================================

#pragma once

#include "EditorPanel.h"

#include <Core/Log.h>
#include <vector>


namespace Dot
{

/// Console panel - displays log messages
class ConsolePanel : public EditorPanel
{
public:
    ConsolePanel();
    ~ConsolePanel();

    void OnImGui() override;

    /// Clear all messages in the console
    void Clear();

private:
    void OnLogMessage(const LogMessage& msg);

    std::vector<LogMessage> m_Messages;
    bool m_AutoScroll = true;
    bool m_ShowTrace = true;
    bool m_ShowInfo = true;
    bool m_ShowWarning = true;
    bool m_ShowError = true;
    bool m_ScrollToBottom = false;
};

} // namespace Dot
