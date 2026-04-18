// =============================================================================
// Dot Engine - Editor Panel Base
// =============================================================================
// Base class for all editor panels (windows).
// =============================================================================

#pragma once

#include <string>

namespace Dot
{

/// Base class for all editor panels
class EditorPanel
{
public:
    EditorPanel(const std::string& name) : m_Name(name) {}
    virtual ~EditorPanel() = default;

    /// Draw the panel UI
    virtual void OnImGui() = 0;

    /// Called when panel is opened
    virtual void OnOpen() {}

    /// Called when panel is closed
    virtual void OnClose() {}

    const std::string& GetName() const { return m_Name; }
    bool IsOpen() const { return m_Open; }
    void SetOpen(bool open) { m_Open = open; }

protected:
    std::string m_Name;
    bool m_Open = true;
};

} // namespace Dot
