// =============================================================================
// Dot Engine - Command System
// =============================================================================
// Base class for all editor commands. Commands are actions that can be
// executed, undone, and registered for menu items, shortcuts, etc.
// =============================================================================

#pragma once

#include <functional>
#include <memory>
#include <string>

namespace Dot
{

// Forward declarations
class World;
struct Entity;

/// Base class for all commands
class Command
{
public:
    virtual ~Command() = default;

    /// Execute the command
    virtual void Execute() = 0;

    /// Undo the command (optional - only for undoable commands)
    virtual void Undo() {}

    /// Can this command be undone?
    virtual bool CanUndo() const { return false; }

    /// Get the display name of this command
    virtual const char* GetName() const = 0;

    /// Get the menu path (e.g., "Create/Primitive")
    virtual const char* GetMenuPath() const { return GetName(); }
};

using CommandPtr = std::unique_ptr<Command>;
using CommandFactory = std::function<CommandPtr(World*, Entity*)>;

} // namespace Dot
