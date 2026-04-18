// =============================================================================
// Dot Engine - Script System
// =============================================================================
// ECS system that manages Lua script execution for entities.
// Handles OnStart, OnUpdate, OnDestroy lifecycle callbacks.
// =============================================================================

#pragma once

#include "Core/Core.h"
#include "Core/ECS/Entity.h"
#include "Core/UI/UISystem.h"

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>

namespace Dot
{

// Forward declarations
class World;
class ScriptRuntime;
class FileWatcher;
class PhysicsSystem;
class CharacterControllerSystem;
class StaticWorldGeometry;
class NavigationSystem;
class UISystem;
struct ScriptComponent;
struct TransformComponent;
struct NameComponent;

/// Callback for script console output
using ScriptConsoleCallback = std::function<void(const std::string&)>;

struct ScriptFeatureConfig
{
    bool enablePhysicsBindings = true;
    bool enableNavigationBindings = true;
    bool enablePerceptionBindings = true;
};

/// Script System - manages Lua scripts attached to entities
class DOT_CORE_API ScriptSystem
{
public:
    ScriptSystem();
    ~ScriptSystem();

    // Non-copyable
    ScriptSystem(const ScriptSystem&) = delete;
    ScriptSystem& operator=(const ScriptSystem&) = delete;

    /// Initialize the script system with a world reference
    bool Initialize(World* world);
    void SetFeatureConfig(const ScriptFeatureConfig& config) { m_FeatureConfig = config; }
    const ScriptFeatureConfig& GetFeatureConfig() const { return m_FeatureConfig; }

    /// Shutdown and cleanup all scripts
    void Shutdown();

    /// Called once when scene starts playing - calls OnStart on all scripts
    void Start();

    /// Called every frame - calls OnUpdate on all scripts
    void Update(float deltaTime);

    /// Called when scene stops playing - calls OnDestroy and unloads scripts
    void Stop();

    /// Reload a specific script (for hot-reloading)
    bool ReloadScript(Entity entity);

    /// Set callback for script console output
    void SetConsoleCallback(ScriptConsoleCallback callback);

    /// Runtime UI accessors
    bool ShowUIScreen(const std::string& assetPath) { return m_UISystem ? m_UISystem->ShowScreen(assetPath) : false; }
    bool PushUIScreen(const std::string& assetPath) { return m_UISystem ? m_UISystem->PushModal(assetPath) : false; }
    bool HideUIScreen(const std::string& assetPath) { return m_UISystem ? m_UISystem->HideScreen(assetPath) : false; }
    void PopUIScreen()
    {
        if (m_UISystem)
            m_UISystem->PopModal();
    }
    void ClearUIScreens()
    {
        if (m_UISystem)
            m_UISystem->ClearScreens();
    }
    bool SetUIWidgetText(const std::string& widgetId, const std::string& text)
    {
        return m_UISystem ? m_UISystem->SetWidgetText(widgetId, text) : false;
    }
    bool SetUIWidgetImage(const std::string& widgetId, const std::string& imagePath)
    {
        return m_UISystem ? m_UISystem->SetWidgetImage(widgetId, imagePath) : false;
    }
    bool SetUIWidgetProgress(const std::string& widgetId, float progress)
    {
        return m_UISystem ? m_UISystem->SetWidgetProgress(widgetId, progress) : false;
    }
    bool SetUIWidgetVisible(const std::string& widgetId, bool visible)
    {
        return m_UISystem ? m_UISystem->SetWidgetVisible(widgetId, visible) : false;
    }
    bool SetUIWidgetEnabled(const std::string& widgetId, bool enabled)
    {
        return m_UISystem ? m_UISystem->SetWidgetEnabled(widgetId, enabled) : false;
    }
    bool FocusUIWidget(const std::string& widgetId)
    {
        return m_UISystem ? m_UISystem->FocusWidget(widgetId) : false;
    }
    void ClearUIFocus()
    {
        if (m_UISystem)
            m_UISystem->ClearFocus();
    }
    bool SetUIOverlayText(const std::string& id, const std::string& text, float x, float y, float r, float g,
                          float b, float a = 1.0f, float fontSize = 16.0f)
    {
        if (!m_UISystem)
            return false;
        m_UISystem->OverlayText(id, text, x, y, r, g, b, a, fontSize);
        return true;
    }
    bool SetUIOverlayImage(const std::string& id, const std::string& imagePath, float x, float y, float width,
                           float height, float r = 1.0f, float g = 1.0f, float b = 1.0f, float a = 1.0f)
    {
        if (!m_UISystem)
            return false;
        m_UISystem->OverlayImage(id, imagePath, x, y, width, height, r, g, b, a);
        return true;
    }
    void ClearUIOverlay()
    {
        if (m_UISystem)
            m_UISystem->ClearOverlay();
    }

    /// Execute arbitrary Lua code (for console)
    bool ExecuteCode(const std::string& code);

    /// Apply damage to an entity's HealthComponent and dispatch script hooks.
    float ApplyDamage(Entity target, float amount, Entity source = kNullEntity);

    /// Heal an entity's HealthComponent.
    float Heal(Entity target, float amount);

    /// Get the script runtime for advanced access
    ScriptRuntime* GetRuntime() { return m_Runtime.get(); }

    /// Get the base path for scripts
    void SetScriptBasePath(const std::string& path) { m_ScriptBasePath = path; }
    const std::string& GetScriptBasePath() const { return m_ScriptBasePath; }

    /// Check if currently playing
    bool IsPlaying() const { return m_IsPlaying; }

    /// Connect the physics system so scripts can consume collision callbacks.
    void SetPhysicsSystem(const PhysicsSystem* physicsSystem) { m_PhysicsSystem = physicsSystem; }
    void SetCharacterControllerSystem(const CharacterControllerSystem* characterControllerSystem)
    {
        m_CharacterControllerSystem = characterControllerSystem;
    }
    void SetStaticWorldGeometry(const StaticWorldGeometry* staticWorldGeometry)
    {
        m_StaticWorldGeometry = staticWorldGeometry;
    }
    void SetNavigationSystem(NavigationSystem* navigationSystem) { m_NavigationSystem = navigationSystem; }
    void SetUISystem(UISystem* uiSystem);
    UISystem* GetUISystem() const { return m_UISystem; }

    // -------------------------------------------------------------------------
    // Hot Reloading
    // -------------------------------------------------------------------------

    /// Enable or disable hot reloading (file watching)
    void SetHotReloadEnabled(bool enabled);

    /// Check if hot reloading is enabled
    bool IsHotReloadEnabled() const { return m_HotReloadEnabled; }

    /// Poll for file changes (call each frame during play mode)
    void PollFileChanges();

    /// Reload all scripts that use a given file path
    void ReloadScriptsByPath(const std::string& path);

private:
    void LoadScript(Entity entity, ScriptComponent& script);
    void UnloadScript(Entity entity, ScriptComponent& script);
    void RegisterEntityBindings();
    void DispatchCollisionEvents();
    void DispatchDamageEvent(Entity target, float amount, Entity source);
    void DispatchDeathEvent(Entity target, Entity source);
    void DispatchNavigationMoveEvents();
    void ClearNavigationMoveCallbacks();
    void ClearTrackedNavigationMovesForEntity(Entity entity);

    std::unique_ptr<ScriptRuntime> m_Runtime;
    std::unique_ptr<FileWatcher> m_FileWatcher;
    World* m_World = nullptr;
    const PhysicsSystem* m_PhysicsSystem = nullptr;
    const CharacterControllerSystem* m_CharacterControllerSystem = nullptr;
    const StaticWorldGeometry* m_StaticWorldGeometry = nullptr;
    NavigationSystem* m_NavigationSystem = nullptr;
    UISystem* m_UISystem = nullptr;
    ScriptConsoleCallback m_ConsoleCallback;
    std::string m_ScriptBasePath = "Assets/Scripts/";
    bool m_IsPlaying = false;
    bool m_HotReloadEnabled = true;
    bool m_Initialized = false;

    // -------------------------------------------------------------------------
    // Camera/Viewport Data (for Lua bindings)
    // -------------------------------------------------------------------------
public:
    /// Camera info for screen-to-world raycasting
    struct CameraInfo
    {
        float posX = 0, posY = 0, posZ = 0;
        float forwardX = 0, forwardY = 0, forwardZ = 1;
        float upX = 0, upY = 1, upZ = 0;
        float rightX = 1, rightY = 0, rightZ = 0;
        float fovDegrees = 60.0f;
        float nearZ = 0.1f;
        float farZ = 1000.0f;
        uint32 renderMask = 1u;
        bool enableViewmodelPass = false;
        uint32 viewmodelMask = 1u << 1;
        float viewmodelFov = 75.0f;
        float viewmodelNearPlane = 0.01f;
    };

    /// Viewport info for mouse coordinate conversion
    struct ViewportInfo
    {
        float x = 0, y = 0; // Viewport position on screen
        float width = 1920, height = 1080;
    };

    /// Set camera info from editor (call each frame before Update)
    void SetCameraInfo(const CameraInfo& info) { m_CameraInfo = info; }
    const CameraInfo& GetCameraInfo() const { return m_CameraInfo; }

    /// Set viewport info from editor
    void SetViewportInfo(const ViewportInfo& info) { m_ViewportInfo = info; }
    const ViewportInfo& GetViewportInfo() const { return m_ViewportInfo; }

private:
    struct CollisionState
    {
        Entity entityA;
        Entity entityB;
        Entity sourceEntity;
        Entity otherEntity;
        float pointX = 0.0f;
        float pointY = 0.0f;
        float pointZ = 0.0f;
        float normalX = 0.0f;
        float normalY = 0.0f;
        float normalZ = 0.0f;
        float depth = 0.0f;
        bool isTrigger = false;
    };

    struct TrackedNavigationMove
    {
        Entity entity = kNullEntity;
    };

    std::unordered_map<unsigned long long, CollisionState> m_PreviousCollisionStates;
    std::unordered_map<uint64, TrackedNavigationMove> m_TrackedNavigationMoves;

    CameraInfo m_CameraInfo;
    ViewportInfo m_ViewportInfo;
    ScriptFeatureConfig m_FeatureConfig;
};

} // namespace Dot

