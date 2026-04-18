#pragma once

#include "Core/Core.h"
#include "Core/Map/StaticWorldGeometry.h"
#include "Core/Scripting/ScriptSystem.h"

#include <memory>

namespace Dot
{

class World;
class PhysicsSystem;
class CharacterControllerSystem;
class PlayerInputSystem;
class NavigationSystem;
class UISystem;

struct SceneRuntimeModuleConfig
{
    bool enablePhysics = true;
    bool enableNavigation = true;
    bool enableScripting = true;
    bool enablePlayerInput = true;
    bool enableUI = true;
    ScriptFeatureConfig scriptFeatures;
};

/// Runtime execution path for scene simulation while in play mode.
class DOT_CORE_API SceneRuntime
{
public:
    SceneRuntime();
    ~SceneRuntime();

    SceneRuntime(const SceneRuntime&) = delete;
    SceneRuntime& operator=(const SceneRuntime&) = delete;

    bool Initialize(World* world);
    void SetModuleConfig(const SceneRuntimeModuleConfig& config) { m_ModuleConfig = config; }
    const SceneRuntimeModuleConfig& GetModuleConfig() const { return m_ModuleConfig; }
    void Shutdown();

    void Start();
    void Pause();
    void Resume();
    void Stop();

    void Tick(float dt);

    bool IsInitialized() const { return m_Initialized; }
    bool IsPlaying() const { return m_Playing; }
    bool IsPaused() const { return m_Playing && m_Paused; }

    void SetCameraInfo(const ScriptSystem::CameraInfo& cameraInfo);
    void SetViewportInfo(const ScriptSystem::ViewportInfo& viewportInfo);
    void SetMouseCaptured(bool captured);
    void SetScriptConsoleCallback(ScriptConsoleCallback callback);
    void SetStaticWorldGeometry(std::shared_ptr<StaticWorldGeometry> staticWorldGeometry);

    ScriptSystem* GetScriptSystem() const { return m_ScriptSystem.get(); }
    PhysicsSystem* GetPhysicsSystem() const { return m_PhysicsSystem.get(); }
    CharacterControllerSystem* GetCharacterControllerSystem() const { return m_CharacterControllerSystem.get(); }
    PlayerInputSystem* GetPlayerInputSystem() const { return m_PlayerInputSystem.get(); }
    NavigationSystem* GetNavigationSystem() const { return m_NavigationSystem.get(); }
    UISystem* GetUISystem() const { return m_UISystem.get(); }

private:
    World* m_World = nullptr;
    std::unique_ptr<PhysicsSystem> m_PhysicsSystem;
    std::unique_ptr<CharacterControllerSystem> m_CharacterControllerSystem;
    std::unique_ptr<PlayerInputSystem> m_PlayerInputSystem;
    std::unique_ptr<NavigationSystem> m_NavigationSystem;
    std::unique_ptr<ScriptSystem> m_ScriptSystem;
    std::unique_ptr<UISystem> m_UISystem;
    std::shared_ptr<StaticWorldGeometry> m_StaticWorldGeometry;
    SceneRuntimeModuleConfig m_ModuleConfig;

    ScriptConsoleCallback m_ScriptConsoleCallback;
    bool m_Initialized = false;
    bool m_Playing = false;
    bool m_Paused = false;
};

} // namespace Dot
