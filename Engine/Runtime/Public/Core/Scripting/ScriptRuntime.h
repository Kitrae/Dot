// =============================================================================
// Dot Engine - Lua Script Runtime
// =============================================================================
// Core Lua state wrapper with error handling and script execution.
// =============================================================================

#pragma once

#include "Core/Core.h"

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

// Forward declare sol types to avoid header pollution
namespace sol
{
class state;
}

namespace Dot
{

struct EntityProxy;

/// Callback for script output (print statements, errors)
using ScriptOutputCallback = std::function<void(const std::string&)>;

/// Lua script runtime - manages the Lua state and script execution
class DOT_CORE_API ScriptRuntime
{
public:
    ScriptRuntime();
    ~ScriptRuntime();

    // Non-copyable
    ScriptRuntime(const ScriptRuntime&) = delete;
    ScriptRuntime& operator=(const ScriptRuntime&) = delete;

    /// Initialize the Lua state and register bindings
    bool Initialize();

    /// Shutdown and cleanup
    void Shutdown();

    /// Execute a Lua script string
    /// @return true if execution succeeded
    bool ExecuteString(const std::string& code);

    /// Validate a Lua script string (syntax check only, does not execute)
    /// @return true if syntax is valid, false otherwise (error in GetLastError())
    bool ValidateString(const std::string& code);

    /// Execute a Lua script file
    /// @return true if execution succeeded
    bool ExecuteFile(const std::string& path);

    /// Load a script file and return a unique reference ID
    /// @return Script reference ID, or -1 on failure
    int LoadScript(const std::string& path);

    /// Unload a previously loaded script
    void UnloadScript(int scriptRef);

    /// Call a function on a loaded script
    /// @return true if the function was called successfully
    bool CallScriptFunction(int scriptRef, const std::string& funcName);

    /// Call a function with a source entity argument.
    bool CallScriptFunction(int scriptRef, const std::string& funcName, const EntityProxy& source);

    /// Call a function with damage amount and source entity arguments.
    bool CallScriptFunction(int scriptRef, const std::string& funcName, float amount, const EntityProxy& source);

    /// Call a function with delta time parameter
    bool CallScriptUpdate(int scriptRef, float deltaTime);

    /// Update all active timers (call once per frame)
    void UpdateTimers(float deltaTime);

    /// Get the raw sol::state (for advanced usage)
    sol::state& GetState();

    /// Set callback for script output
    void SetOutputCallback(ScriptOutputCallback callback) { m_OutputCallback = callback; }

    /// Get last error message
    const std::string& GetLastError() const { return m_LastError; }

    /// Check if runtime is initialized
    bool IsInitialized() const { return m_Initialized; }

private:
    void RegisterCoreBindings();
    void RegisterMathBindings();
    void RegisterInputBindings();
    void RegisterLogBindings();
    void RegisterTimerBindings();

    void LogOutput(const std::string& message);
    void LogError(const std::string& error);

    std::unique_ptr<sol::state> m_State;
    ScriptOutputCallback m_OutputCallback;
    std::string m_LastError;
    bool m_Initialized = false;

    // Timer system
    struct Timer
    {
        int id;
        float delay;
        float elapsed;
        bool repeating;
        bool cancelled;
    };
    std::vector<Timer> m_Timers;
    int m_NextTimerId = 1;

    // Script reference counter
    int m_NextScriptRef = 1;
    std::unordered_map<int, std::string> m_LoadedScripts;
};

} // namespace Dot
