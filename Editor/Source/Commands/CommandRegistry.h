// =============================================================================
// Dot Engine - Command Registry
// =============================================================================
// Singleton registry for all editor commands. Handles registration,
// menu building, undo/redo, and command execution.
// =============================================================================

#pragma once

#include "Command.h"

#include <deque>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace Dot
{

// Forward declarations
class World;
struct Entity;

/// Info about a registered command
struct CommandInfo
{
    std::string name;       // Display name (e.g., "Primitive")
    std::string menuPath;   // Full menu path (e.g., "Create/Primitive")
    CommandFactory factory; // Factory to create command instances
};

/// Singleton registry for all commands with undo/redo support
class CommandRegistry
{
public:
    static CommandRegistry& Get()
    {
        static CommandRegistry instance;
        return instance;
    }

    /// Register a command with a menu path
    /// Path format: "Category/Subcategory/Name" (e.g., "Create/Primitive")
    void Register(const std::string& menuPath, CommandFactory factory)
    {
        CommandInfo info;
        info.menuPath = menuPath;
        info.factory = factory;

        // Extract display name from path (last segment)
        size_t lastSlash = menuPath.rfind('/');
        info.name = (lastSlash != std::string::npos) ? menuPath.substr(lastSlash + 1) : menuPath;

        m_Commands[menuPath] = info;
    }

    /// Execute a command by its menu path and add to undo stack
    void Execute(const std::string& menuPath, World* world, Entity* outEntity = nullptr)
    {
        auto it = m_Commands.find(menuPath);
        if (it != m_Commands.end())
        {
            auto cmd = it->second.factory(world, outEntity);
            if (cmd)
            {
                cmd->Execute();
                if (cmd->CanUndo())
                {
                    m_UndoStack.push_back(std::move(cmd));
                    m_RedoStack.clear(); // Clear redo on new action
                    // Limit undo history
                    while (m_UndoStack.size() > m_MaxUndoHistory)
                        m_UndoStack.pop_front();
                }
            }
        }
    }

    /// Execute any command and add to undo stack if undoable
    void ExecuteCommand(CommandPtr cmd)
    {
        if (!cmd)
            return;
        cmd->Execute();
        if (cmd->CanUndo())
        {
            m_UndoStack.push_back(std::move(cmd));
            m_RedoStack.clear();
            while (m_UndoStack.size() > m_MaxUndoHistory)
                m_UndoStack.pop_front();
        }
    }

    /// Push a command to the undo stack without executing (for actions already applied)
    void PushCommand(CommandPtr cmd)
    {
        if (!cmd)
            return;
        if (cmd->CanUndo())
        {
            m_UndoStack.push_back(std::move(cmd));
            m_RedoStack.clear();
            while (m_UndoStack.size() > m_MaxUndoHistory)
                m_UndoStack.pop_front();
        }
    }

    /// Undo the last command
    void Undo()
    {
        if (m_UndoStack.empty())
            return;
        auto cmd = std::move(m_UndoStack.back());
        m_UndoStack.pop_back();
        cmd->Undo();
        m_RedoStack.push_back(std::move(cmd));
    }

    /// Redo the last undone command
    void Redo()
    {
        if (m_RedoStack.empty())
            return;
        auto cmd = std::move(m_RedoStack.back());
        m_RedoStack.pop_back();
        cmd->Execute();
        m_UndoStack.push_back(std::move(cmd));
    }

    /// Can undo?
    bool CanUndo() const { return !m_UndoStack.empty(); }

    /// Can redo?
    bool CanRedo() const { return !m_RedoStack.empty(); }

    /// Get undo description
    const char* GetUndoDescription() const { return m_UndoStack.empty() ? nullptr : m_UndoStack.back()->GetName(); }

    /// Get redo description
    const char* GetRedoDescription() const { return m_RedoStack.empty() ? nullptr : m_RedoStack.back()->GetName(); }

    /// Clear undo/redo history (e.g., on scene change)
    void ClearHistory()
    {
        m_UndoStack.clear();
        m_RedoStack.clear();
    }

    /// Draw ImGui context menu for a category
    /// Returns the created entity if any command created one
    Entity DrawContextMenu(const std::string& category, World* world);

    /// Get all commands in a category
    std::vector<const CommandInfo*> GetCommandsInCategory(const std::string& category) const
    {
        std::vector<const CommandInfo*> result;
        std::string prefix = category + "/";

        for (const auto& [path, info] : m_Commands)
        {
            if (path.find(prefix) == 0 || path == category)
            {
                result.push_back(&info);
            }
        }
        return result;
    }

private:
    CommandRegistry() = default;

    std::map<std::string, CommandInfo> m_Commands;
    std::deque<CommandPtr> m_UndoStack;
    std::deque<CommandPtr> m_RedoStack;
    size_t m_MaxUndoHistory = 100;
};

} // namespace Dot
