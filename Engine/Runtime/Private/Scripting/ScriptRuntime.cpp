// =============================================================================
// Dot Engine - Lua Script Runtime Implementation
// =============================================================================

#include "Core/Scripting/ScriptRuntime.h"

#include "Core/Log.h"
#include "Core/Scripting/EntityProxy.h"
#include "Core/Math/Vec2.h"
#include "Core/Math/Vec3.h"
#include "Core/Math/Vec4.h"

#define SOL_ALL_SAFETIES_ON 1

// Suppress warnings from third-party sol2 headers
#ifdef _MSC_VER
    #pragma warning(push)
    #pragma warning(disable : 4100)  // unreferenced formal parameter
    #pragma warning(disable : 4127)  // conditional expression is constant
    #pragma warning(disable : 4189)  // local variable is initialized but not referenced
    #pragma warning(disable : 4244)  // conversion from 'type1' to 'type2', possible loss of data
    #pragma warning(disable : 4702)  // unreachable code
    #pragma warning(disable : 4840)  // non-portable use of class as argument to a variadic function
    #pragma warning(disable : 5054)  // deprecated enum arithmetic
    #pragma warning(disable : 4996)  // deprecated functions
    #pragma warning(disable : 4505)  // unreferenced local function has been removed
    #pragma warning(disable : 26495) // uninitialized member variable
    #pragma warning(disable : 26451) // arithmetic overflow
#endif

#include <sol/sol.hpp>

#ifdef _MSC_VER
    #pragma warning(pop)
#endif

#include <algorithm>
#include <fstream>
#include <sstream>
#include <unordered_map>

#ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

namespace Dot
{

namespace
{

bool RuntimeAppHasInputFocus()
{
    HWND foregroundWindow = GetForegroundWindow();
    if (!foregroundWindow)
        return false;

    DWORD foregroundProcessId = 0;
    GetWindowThreadProcessId(foregroundWindow, &foregroundProcessId);
    return foregroundProcessId == GetCurrentProcessId();
}

bool IsVirtualKeyDownFocused(int vk)
{
    return RuntimeAppHasInputFocus() && (GetAsyncKeyState(vk) & 0x8000) != 0;
}

} // namespace

// =============================================================================
// Key name to Virtual Key Code mapping for Lua input
// =============================================================================
static int KeyNameToVK(const std::string& key)
{
    // Single character keys (A-Z, 0-9)
    if (key.length() == 1)
    {
        char c = key[0];
        if (c >= 'A' && c <= 'Z')
            return c;
        if (c >= 'a' && c <= 'z')
            return c - 32; // Convert to uppercase
        if (c >= '0' && c <= '9')
            return c;
    }

    // Special keys
    if (key == "Space" || key == "space")
        return VK_SPACE;
    if (key == "Enter" || key == "enter" || key == "Return")
        return VK_RETURN;
    if (key == "Escape" || key == "escape" || key == "Esc")
        return VK_ESCAPE;
    if (key == "Tab" || key == "tab")
        return VK_TAB;
    if (key == "Backspace" || key == "backspace")
        return VK_BACK;
    if (key == "Delete" || key == "delete")
        return VK_DELETE;
    if (key == "Insert" || key == "insert")
        return VK_INSERT;
    if (key == "Home" || key == "home")
        return VK_HOME;
    if (key == "End" || key == "end")
        return VK_END;
    if (key == "PageUp" || key == "pageup")
        return VK_PRIOR;
    if (key == "PageDown" || key == "pagedown")
        return VK_NEXT;

    // Arrow keys
    if (key == "Up" || key == "up" || key == "ArrowUp")
        return VK_UP;
    if (key == "Down" || key == "down" || key == "ArrowDown")
        return VK_DOWN;
    if (key == "Left" || key == "left" || key == "ArrowLeft")
        return VK_LEFT;
    if (key == "Right" || key == "right" || key == "ArrowRight")
        return VK_RIGHT;

    // Modifier keys
    if (key == "Shift" || key == "shift")
        return VK_SHIFT;
    if (key == "Control" || key == "control" || key == "Ctrl" || key == "ctrl")
        return VK_CONTROL;
    if (key == "Alt" || key == "alt")
        return VK_MENU;
    if (key == "LShift" || key == "lshift")
        return VK_LSHIFT;
    if (key == "RShift" || key == "rshift")
        return VK_RSHIFT;
    if (key == "LControl" || key == "lcontrol" || key == "LCtrl")
        return VK_LCONTROL;
    if (key == "RControl" || key == "rcontrol" || key == "RCtrl")
        return VK_RCONTROL;

    // Function keys
    if (key == "F1")
        return VK_F1;
    if (key == "F2")
        return VK_F2;
    if (key == "F3")
        return VK_F3;
    if (key == "F4")
        return VK_F4;
    if (key == "F5")
        return VK_F5;
    if (key == "F6")
        return VK_F6;
    if (key == "F7")
        return VK_F7;
    if (key == "F8")
        return VK_F8;
    if (key == "F9")
        return VK_F9;
    if (key == "F10")
        return VK_F10;
    if (key == "F11")
        return VK_F11;
    if (key == "F12")
        return VK_F12;

    return 0; // Unknown key
}

// Track previous key states for IsKeyPressed/Released detection
static std::unordered_map<int, bool> s_PreviousKeyStates;

ScriptRuntime::ScriptRuntime() = default;

ScriptRuntime::~ScriptRuntime()
{
    Shutdown();
}

bool ScriptRuntime::Initialize()
{
    if (m_Initialized)
        return true;

    try
    {
        m_State = std::make_unique<sol::state>();

        // Open standard Lua libraries
        m_State->open_libraries(sol::lib::base, sol::lib::math, sol::lib::string, sol::lib::table, sol::lib::coroutine);

        // Register engine bindings
        RegisterCoreBindings();
        RegisterMathBindings();
        RegisterInputBindings();
        RegisterLogBindings();
        RegisterTimerBindings();

        m_Initialized = true;
        DOT_LOG_INFO("ScriptRuntime initialized successfully");
        return true;
    }
    catch (const std::exception& e)
    {
        m_LastError = std::string("Failed to initialize Lua: ") + e.what();
        DOT_LOG_ERROR("%s", m_LastError.c_str());
        return false;
    }
}

void ScriptRuntime::Shutdown()
{
    if (!m_Initialized)
        return;

    m_LoadedScripts.clear();
    m_State.reset();
    m_Initialized = false;
    DOT_LOG_INFO("ScriptRuntime shutdown");
}

bool ScriptRuntime::ExecuteString(const std::string& code)
{
    if (!m_Initialized)
    {
        m_LastError = "ScriptRuntime not initialized";
        return false;
    }

    try
    {
        auto result = m_State->safe_script(code, sol::script_pass_on_error);
        if (!result.valid())
        {
            sol::error err = result;
            m_LastError = err.what();
            LogError(m_LastError);
            return false;
        }
        return true;
    }
    catch (const std::exception& e)
    {
        m_LastError = e.what();
        LogError(m_LastError);
        return false;
    }
}

bool ScriptRuntime::ValidateString(const std::string& code)
{
    if (!m_Initialized)
    {
        m_LastError = "ScriptRuntime not initialized";
        return false;
    }

    try
    {
        // Use load to check syntax without executing
        auto result = m_State->load(code);
        if (!result.valid())
        {
            sol::error err = result;
            m_LastError = err.what();
            return false;
        }
        m_LastError.clear();
        return true;
    }
    catch (const std::exception& e)
    {
        m_LastError = e.what();
        return false;
    }
}

bool ScriptRuntime::ExecuteFile(const std::string& path)
{
    if (!m_Initialized)
    {
        m_LastError = "ScriptRuntime not initialized";
        return false;
    }

    try
    {
        auto result = m_State->safe_script_file(path, sol::script_pass_on_error);
        if (!result.valid())
        {
            sol::error err = result;
            m_LastError = err.what();
            LogError(m_LastError);
            return false;
        }
        return true;
    }
    catch (const std::exception& e)
    {
        m_LastError = e.what();
        LogError(m_LastError);
        return false;
    }
}

int ScriptRuntime::LoadScript(const std::string& path)
{
    if (!m_Initialized)
        return -1;

    // Read file content
    std::ifstream file(path);
    if (!file.is_open())
    {
        m_LastError = "Could not open script file: " + path;
        LogError(m_LastError);
        return -1;
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string code = buffer.str();
    file.close();

    // Create a unique table for this script instance
    int scriptRef = m_NextScriptRef++;
    std::string tableName = "script_" + std::to_string(scriptRef);

    // Create script environment compatible with LuaJIT (Lua 5.1)
    // LuaJIT uses setfenv, not _ENV
    std::string wrappedCode = tableName + " = setmetatable({}, {__index = _G})\n" + "do\n" +
                              "  local _ENV = " + tableName + "\n" + "  setfenv(1, _ENV)\n" + code + "\n" + "end\n";

    try
    {
        auto result = m_State->safe_script(wrappedCode, sol::script_pass_on_error);
        if (!result.valid())
        {
            sol::error err = result;
            m_LastError = err.what();
            LogError(m_LastError);
            return -1;
        }

        m_LoadedScripts[scriptRef] = tableName;
        return scriptRef;
    }
    catch (const std::exception& e)
    {
        m_LastError = e.what();
        LogError(m_LastError);
        return -1;
    }
}

void ScriptRuntime::UnloadScript(int scriptRef)
{
    auto it = m_LoadedScripts.find(scriptRef);
    if (it != m_LoadedScripts.end())
    {
        // Set table to nil to allow garbage collection
        (*m_State)[it->second] = sol::nil;
        m_LoadedScripts.erase(it);
    }
}

bool ScriptRuntime::CallScriptFunction(int scriptRef, const std::string& funcName)
{
    auto it = m_LoadedScripts.find(scriptRef);
    if (it == m_LoadedScripts.end())
        return false;

    try
    {
        sol::table scriptTable = (*m_State)[it->second];
        sol::function func = scriptTable[funcName];

        if (!func.valid())
            return false; // Function doesn't exist (not an error)

        auto result = func(scriptTable);
        if (!result.valid())
        {
            sol::error err = result;
            m_LastError = err.what();
            LogError(m_LastError);
            return false;
        }
        return true;
    }
    catch (const std::exception& e)
    {
        m_LastError = e.what();
        LogError(m_LastError);
        return false;
    }
}

bool ScriptRuntime::CallScriptFunction(int scriptRef, const std::string& funcName, const EntityProxy& source)
{
    auto it = m_LoadedScripts.find(scriptRef);
    if (it == m_LoadedScripts.end())
        return false;

    try
    {
        sol::table scriptTable = (*m_State)[it->second];
        sol::function func = scriptTable[funcName];

        if (!func.valid())
            return false;

        auto result = func(source);
        if (!result.valid())
        {
            sol::error err = result;
            m_LastError = err.what();
            LogError(m_LastError);
            return false;
        }
        return true;
    }
    catch (const std::exception& e)
    {
        m_LastError = e.what();
        LogError(m_LastError);
        return false;
    }
}

bool ScriptRuntime::CallScriptFunction(int scriptRef, const std::string& funcName, float amount, const EntityProxy& source)
{
    auto it = m_LoadedScripts.find(scriptRef);
    if (it == m_LoadedScripts.end())
        return false;

    try
    {
        sol::table scriptTable = (*m_State)[it->second];
        sol::function func = scriptTable[funcName];

        if (!func.valid())
            return false;

        auto result = func(amount, source);
        if (!result.valid())
        {
            sol::error err = result;
            m_LastError = err.what();
            LogError(m_LastError);
            return false;
        }
        return true;
    }
    catch (const std::exception& e)
    {
        m_LastError = e.what();
        LogError(m_LastError);
        return false;
    }
}

bool ScriptRuntime::CallScriptUpdate(int scriptRef, float deltaTime)
{
    auto it = m_LoadedScripts.find(scriptRef);
    if (it == m_LoadedScripts.end())
        return false;

    try
    {
        sol::table scriptTable = (*m_State)[it->second];
        sol::function func = scriptTable["OnUpdate"];

        if (!func.valid())
            return true; // No OnUpdate is fine

        // Call OnUpdate with just deltaTime (not the table)
        auto result = func(deltaTime);
        if (!result.valid())
        {
            sol::error err = result;
            m_LastError = err.what();
            LogError(m_LastError);
            return false;
        }
        return true;
    }
    catch (const std::exception& e)
    {
        m_LastError = e.what();
        LogError(m_LastError);
        return false;
    }
}

sol::state& ScriptRuntime::GetState()
{
    return *m_State;
}

void ScriptRuntime::RegisterCoreBindings()
{
    // Override print to go through our callback
    m_State->set_function("print",
                          [this](sol::variadic_args va)
                          {
                              std::string output;
                              for (auto v : va)
                              {
                                  if (!output.empty())
                                      output += "\t";
                                  output += m_State->get<sol::function>("tostring")(v);
                              }
                              LogOutput(output);
                          });
}

void ScriptRuntime::RegisterMathBindings()
{
    // Vec2
    m_State->new_usertype<Vec2>(
        "Vec2", sol::constructors<Vec2(), Vec2(float, float)>(), "x", &Vec2::x, "y", &Vec2::y,
        sol::meta_function::addition, [](const Vec2& a, const Vec2& b) { return Vec2(a.x + b.x, a.y + b.y); },
        sol::meta_function::subtraction, [](const Vec2& a, const Vec2& b) { return Vec2(a.x - b.x, a.y - b.y); },
        sol::meta_function::multiplication, [](const Vec2& a, float s) { return Vec2(a.x * s, a.y * s); });

    // Vec3 - with explicit call constructor for LuaJIT compatibility
    m_State->new_usertype<Vec3>(
        "Vec3", sol::call_constructor, sol::constructors<Vec3(), Vec3(float, float, float)>(), "x", &Vec3::x, "y",
        &Vec3::y, "z", &Vec3::z, sol::meta_function::addition, [](const Vec3& a, const Vec3& b)
        { return Vec3(a.x + b.x, a.y + b.y, a.z + b.z); }, sol::meta_function::subtraction,
        [](const Vec3& a, const Vec3& b) { return Vec3(a.x - b.x, a.y - b.y, a.z - b.z); },
        sol::meta_function::multiplication, [](const Vec3& a, float s) { return Vec3(a.x * s, a.y * s, a.z * s); },
        sol::meta_function::to_string, [](const Vec3& v)
        { return "Vec3(" + std::to_string(v.x) + ", " + std::to_string(v.y) + ", " + std::to_string(v.z) + ")"; },
        "Dot", [](const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }, "Length",
        [](const Vec3& v) { return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z); }, "Normalized",
        [](const Vec3& v)
        {
            float len = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
            return len > 0.0001f ? Vec3(v.x / len, v.y / len, v.z / len) : Vec3();
        });

    // Vec4
    m_State->new_usertype<Vec4>("Vec4", sol::constructors<Vec4(), Vec4(float, float, float, float)>(), "x", &Vec4::x,
                                "y", &Vec4::y, "z", &Vec4::z, "w", &Vec4::w);

    // Math utility table
    sol::table mathTable = m_State->create_named_table("Math");

    // Math.Lerp(a, b, t) - linear interpolation
    mathTable["Lerp"] = [](float a, float b, float t) -> float { return a + (b - a) * t; };

    // Math.LerpVec3(a, b, t) - lerp for vectors
    mathTable["LerpVec3"] = [](const Vec3& a, const Vec3& b, float t) -> Vec3
    { return Vec3(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t); };

    // Math.Clamp(value, min, max)
    mathTable["Clamp"] = [](float value, float minVal, float maxVal) -> float
    {
        if (value < minVal)
            return minVal;
        if (value > maxVal)
            return maxVal;
        return value;
    };

    // Math.Random() - random 0-1
    mathTable["Random"] = []() -> float { return static_cast<float>(rand()) / static_cast<float>(RAND_MAX); };

    // Math.RandomRange(min, max) - random in range
    mathTable["RandomRange"] = [](float minVal, float maxVal) -> float
    {
        float t = static_cast<float>(rand()) / static_cast<float>(RAND_MAX);
        return minVal + (maxVal - minVal) * t;
    };

    // Math.RandomInt(min, max) - random integer in range (inclusive)
    mathTable["RandomInt"] = [](int minVal, int maxVal) -> int { return minVal + (rand() % (maxVal - minVal + 1)); };

    // Math.Sign(value)
    mathTable["Sign"] = [](float value) -> float
    {
        if (value > 0)
            return 1.0f;
        if (value < 0)
            return -1.0f;
        return 0.0f;
    };

    // Math.Abs(value)
    mathTable["Abs"] = [](float value) -> float { return value < 0 ? -value : value; };

    // Math.Distance(a, b) - distance between two Vec3s
    mathTable["Distance"] = [](const Vec3& a, const Vec3& b) -> float
    {
        float dx = b.x - a.x;
        float dy = b.y - a.y;
        float dz = b.z - a.z;
        return std::sqrt(dx * dx + dy * dy + dz * dz);
    };
}

void ScriptRuntime::RegisterInputBindings()
{
    // Create Input table
    sol::table inputTable = m_State->create_named_table("Input");

    // =========================================================================
    // Keyboard Input
    // =========================================================================

    // IsKeyDown - true while key is held
    inputTable["IsKeyDown"] = [](const std::string& key) -> bool
    {
        int vk = KeyNameToVK(key);
        if (vk == 0)
            return false;
        return IsVirtualKeyDownFocused(vk);
    };

    // IsKeyPressed - true only on the frame the key was pressed
    inputTable["IsKeyPressed"] = [](const std::string& key) -> bool
    {
        int vk = KeyNameToVK(key);
        if (vk == 0)
            return false;

        bool isDown = IsVirtualKeyDownFocused(vk);
        bool wasDown = s_PreviousKeyStates[vk];

        // Just pressed = down now but wasn't before
        return isDown && !wasDown;
    };

    // IsKeyReleased - true only on the frame the key was released
    inputTable["IsKeyReleased"] = [](const std::string& key) -> bool
    {
        int vk = KeyNameToVK(key);
        if (vk == 0)
            return false;

        bool isDown = IsVirtualKeyDownFocused(vk);
        bool wasDown = s_PreviousKeyStates[vk];

        // Just released = not down now but was before
        return !isDown && wasDown;
    };

    // =========================================================================
    // Mouse Input
    // =========================================================================

    // IsMouseButtonDown - 0=left, 1=right, 2=middle
    inputTable["IsMouseButtonDown"] = [](int button) -> bool
    {
        int vk;
        switch (button)
        {
            case 0:
                vk = VK_LBUTTON;
                break;
            case 1:
                vk = VK_RBUTTON;
                break;
            case 2:
                vk = VK_MBUTTON;
                break;
            default:
                return false;
        }
        return IsVirtualKeyDownFocused(vk);
    };

    // IsMouseButtonPressed - true only on the frame the button was pressed
    inputTable["IsMouseButtonPressed"] = [](int button) -> bool
    {
        int vk;
        switch (button)
        {
            case 0:
                vk = VK_LBUTTON;
                break;
            case 1:
                vk = VK_RBUTTON;
                break;
            case 2:
                vk = VK_MBUTTON;
                break;
            default:
                return false;
        }
        bool isDown = IsVirtualKeyDownFocused(vk);
        bool wasDown = s_PreviousKeyStates[vk];
        return isDown && !wasDown;
    };

    // IsMouseButtonReleased - true only on the frame the button was released
    inputTable["IsMouseButtonReleased"] = [](int button) -> bool
    {
        int vk;
        switch (button)
        {
            case 0:
                vk = VK_LBUTTON;
                break;
            case 1:
                vk = VK_RBUTTON;
                break;
            case 2:
                vk = VK_MBUTTON;
                break;
            default:
                return false;
        }
        bool isDown = IsVirtualKeyDownFocused(vk);
        bool wasDown = s_PreviousKeyStates[vk];
        return !isDown && wasDown;
    };

    // GetMousePosition - returns screen position
    inputTable["GetMousePosition"] = []() -> Vec2
    {
        POINT pt;
        if (GetCursorPos(&pt))
        {
            return Vec2(static_cast<float>(pt.x), static_cast<float>(pt.y));
        }
        return Vec2(0, 0);
    };

    // GetMouseDelta - for FPS-style controls (simplified version)
    static POINT s_LastMousePos = {0, 0};
    static bool s_FirstMouse = true;

    inputTable["GetMouseDelta"] = []() -> Vec2
    {
        if (!RuntimeAppHasInputFocus())
        {
            s_FirstMouse = true;
            return Vec2(0, 0);
        }

        POINT pt;
        if (GetCursorPos(&pt))
        {
            if (s_FirstMouse)
            {
                s_LastMousePos = pt;
                s_FirstMouse = false;
                return Vec2(0, 0);
            }

            Vec2 delta(static_cast<float>(pt.x - s_LastMousePos.x), static_cast<float>(pt.y - s_LastMousePos.y));
            s_LastMousePos = pt;
            return delta;
        }
        return Vec2(0, 0);
    };

    // =========================================================================
    // Utility
    // =========================================================================

    // UpdateKeyStates - call at end of frame to track pressed/released
    inputTable["_UpdateStates"] = []()
    {
        // Update tracked keys
        for (int vk = 0; vk < 256; ++vk)
        {
            s_PreviousKeyStates[vk] = IsVirtualKeyDownFocused(vk);
        }
    };
}

void ScriptRuntime::RegisterLogBindings()
{
    // Create Log table
    sol::table logTable = m_State->create_named_table("Log");

    logTable["Info"] = [this](const std::string& msg)
    {
        DOT_LOG_INFO("[Script] %s", msg.c_str());
        LogOutput("[INFO] " + msg);
    };

    logTable["Warn"] = [this](const std::string& msg)
    {
        DOT_LOG_WARN("[Script] %s", msg.c_str());
        LogOutput("[WARN] " + msg);
    };

    logTable["Error"] = [this](const std::string& msg)
    {
        DOT_LOG_ERROR("[Script] %s", msg.c_str());
        LogOutput("[ERROR] " + msg);
    };
}

void ScriptRuntime::LogOutput(const std::string& message)
{
    if (m_OutputCallback)
        m_OutputCallback(message);
    else
        DOT_LOG_INFO("[Lua] %s", message.c_str());
}

void ScriptRuntime::LogError(const std::string& error)
{
    DOT_LOG_ERROR("[Lua Error] %s", error.c_str());
    if (m_OutputCallback)
        m_OutputCallback("[ERROR] " + error);
}

// =============================================================================
// Timer System
// =============================================================================

// Storage for timer callbacks (sol::function can't be in header due to forward decl)
static std::unordered_map<int, sol::function> s_TimerCallbacks;

void ScriptRuntime::RegisterTimerBindings()
{
    sol::table timerTable = m_State->create_named_table("Timer");

    // Timer.After(seconds, callback) - call once after delay
    timerTable["After"] = [this](float delay, sol::function callback) -> int
    {
        int id = m_NextTimerId++;
        Timer timer;
        timer.id = id;
        timer.delay = delay;
        timer.elapsed = 0.0f;
        timer.repeating = false;
        timer.cancelled = false;
        m_Timers.push_back(timer);
        s_TimerCallbacks[id] = callback;
        return id;
    };

    // Timer.Every(seconds, callback) - call repeatedly
    timerTable["Every"] = [this](float delay, sol::function callback) -> int
    {
        int id = m_NextTimerId++;
        Timer timer;
        timer.id = id;
        timer.delay = delay;
        timer.elapsed = 0.0f;
        timer.repeating = true;
        timer.cancelled = false;
        m_Timers.push_back(timer);
        s_TimerCallbacks[id] = callback;
        return id;
    };

    // Timer.Cancel(id) - cancel a timer
    timerTable["Cancel"] = [this](int id)
    {
        for (auto& timer : m_Timers)
        {
            if (timer.id == id)
            {
                timer.cancelled = true;
                break;
            }
        }
        s_TimerCallbacks.erase(id);
    };

    // Timer.CancelAll() - cancel all timers
    timerTable["CancelAll"] = [this]()
    {
        m_Timers.clear();
        s_TimerCallbacks.clear();
    };
}

void ScriptRuntime::UpdateTimers(float deltaTime)
{
    if (!m_Initialized || m_Timers.empty())
        return;

    // Update all timers
    for (auto& timer : m_Timers)
    {
        if (timer.cancelled)
            continue;

        timer.elapsed += deltaTime;

        if (timer.elapsed >= timer.delay)
        {
            // Time to fire!
            auto it = s_TimerCallbacks.find(timer.id);
            if (it != s_TimerCallbacks.end())
            {
                try
                {
                    auto result = it->second();
                    if (!result.valid())
                    {
                        sol::error err = result;
                        LogError(std::string("Timer callback error: ") + err.what());
                    }
                }
                catch (const std::exception& e)
                {
                    LogError(std::string("Timer exception: ") + e.what());
                }
            }

            if (timer.repeating)
            {
                // Reset for next iteration
                timer.elapsed = 0.0f;
            }
            else
            {
                // Mark for removal
                timer.cancelled = true;
                s_TimerCallbacks.erase(timer.id);
            }
        }
    }

    // Remove cancelled timers
    m_Timers.erase(std::remove_if(m_Timers.begin(), m_Timers.end(), [](const Timer& t) { return t.cancelled; }),
                   m_Timers.end());
}

} // namespace Dot
