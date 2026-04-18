#include "Core/Scene/SceneRuntime.h"

#include "Core/ECS/World.h"
#include "Core/Input/PlayerInputSystem.h"
#include "Core/Navigation/NavigationSystem.h"
#include "Core/Physics/CharacterControllerSystem.h"
#include "Core/Physics/PhysicsSystem.h"
#include "Core/Scene/CameraComponent.h"
#include "Core/Scene/Components.h"
#include "Core/Scripting/ScriptSystem.h"
#include "Core/UI/UISystem.h"

#include <cmath>
#include <utility>

namespace Dot
{

SceneRuntime::SceneRuntime() = default;

SceneRuntime::~SceneRuntime()
{
    Shutdown();
}

bool SceneRuntime::Initialize(World* world)
{
    if (m_Initialized)
    {
        return true;
    }

    if (world == nullptr)
    {
        return false;
    }

    m_World = world;

    const bool enablePhysics = m_ModuleConfig.enablePhysics;
    const bool enableNavigation = enablePhysics && m_ModuleConfig.enableNavigation;
    const bool enablePlayerInput = enablePhysics && m_ModuleConfig.enablePlayerInput;
    const bool enableScripting = m_ModuleConfig.enableScripting;
    const bool enableUI = m_ModuleConfig.enableUI;

    if (enablePhysics)
    {
        m_PhysicsSystem = std::make_unique<PhysicsSystem>();
        m_PhysicsSystem->Initialize();

        m_CharacterControllerSystem = std::make_unique<CharacterControllerSystem>();
        m_CharacterControllerSystem->SetStaticWorldGeometry(m_StaticWorldGeometry.get());

        if (enablePlayerInput)
            m_PlayerInputSystem = std::make_unique<PlayerInputSystem>();

        if (enableNavigation)
        {
            m_NavigationSystem = std::make_unique<NavigationSystem>();
            m_NavigationSystem->SetStaticWorldGeometry(m_StaticWorldGeometry.get());
        }
    }

    if (enableUI)
    {
        m_UISystem = std::make_unique<UISystem>();
        if (!m_UISystem->Initialize(m_World))
        {
            m_UISystem.reset();
            m_NavigationSystem.reset();
            m_PlayerInputSystem.reset();
            m_CharacterControllerSystem.reset();
            if (m_PhysicsSystem)
            {
                m_PhysicsSystem->Shutdown();
                m_PhysicsSystem.reset();
            }
            m_World = nullptr;
            return false;
        }
    }

    if (enableScripting)
    {
        m_ScriptSystem = std::make_unique<ScriptSystem>();
        ScriptFeatureConfig scriptFeatures = m_ModuleConfig.scriptFeatures;
        scriptFeatures.enablePhysicsBindings = enablePhysics && scriptFeatures.enablePhysicsBindings;
        scriptFeatures.enableNavigationBindings = enableNavigation && scriptFeatures.enableNavigationBindings;
        scriptFeatures.enablePerceptionBindings = enablePhysics && enableScripting && scriptFeatures.enablePerceptionBindings;
        m_ScriptSystem->SetFeatureConfig(scriptFeatures);
        if (!m_ScriptSystem->Initialize(m_World))
        {
            m_ScriptSystem.reset();
            m_NavigationSystem.reset();
            m_PlayerInputSystem.reset();
            m_CharacterControllerSystem.reset();
            if (m_PhysicsSystem)
            {
                m_PhysicsSystem->Shutdown();
                m_PhysicsSystem.reset();
            }
            m_World = nullptr;
            return false;
        }

        m_ScriptSystem->SetPhysicsSystem(m_PhysicsSystem.get());
        m_ScriptSystem->SetCharacterControllerSystem(m_CharacterControllerSystem.get());
        m_ScriptSystem->SetStaticWorldGeometry(m_StaticWorldGeometry.get());
        m_ScriptSystem->SetNavigationSystem(m_NavigationSystem.get());
        m_ScriptSystem->SetUISystem(m_UISystem.get());

        if (m_ScriptConsoleCallback)
        {
            m_ScriptSystem->SetConsoleCallback(m_ScriptConsoleCallback);
        }
    }

    m_Initialized = true;
    m_Playing = false;
    m_Paused = false;
    return true;
}

void SceneRuntime::Shutdown()
{
    if (!m_Initialized)
    {
        return;
    }

    Stop();

    if (m_ScriptSystem)
    {
        m_ScriptSystem->Shutdown();
        m_ScriptSystem.reset();
    }

    if (m_UISystem)
    {
        m_UISystem->Shutdown();
        m_UISystem.reset();
    }

    m_PlayerInputSystem.reset();
    m_CharacterControllerSystem.reset();
    m_NavigationSystem.reset();

    if (m_PhysicsSystem)
    {
        m_PhysicsSystem->Shutdown();
        m_PhysicsSystem.reset();
    }

    m_World = nullptr;
    m_Initialized = false;
    m_Playing = false;
    m_Paused = false;
}

void SceneRuntime::Start()
{
    if (!m_Initialized)
    {
        return;
    }

    if (m_Playing)
    {
        m_Paused = false;
        return;
    }

    m_Playing = true;
    m_Paused = false;

    if (m_ScriptSystem)
    {
        m_ScriptSystem->Start();
    }
}

void SceneRuntime::Pause()
{
    if (!m_Playing)
    {
        return;
    }

    m_Paused = true;
}

void SceneRuntime::Resume()
{
    if (!m_Playing)
    {
        return;
    }

    m_Paused = false;
}

void SceneRuntime::Stop()
{
    if (!m_Playing)
    {
        return;
    }

    if (m_ScriptSystem)
    {
        m_ScriptSystem->Stop();
    }

    m_Playing = false;
    m_Paused = false;
}

void SceneRuntime::Tick(float dt)
{
    if (!m_Playing || m_Paused || m_World == nullptr)
    {
        return;
    }

    if (m_PhysicsSystem)
    {
        m_PhysicsSystem->Update(*m_World, dt);
    }

    if (m_CharacterControllerSystem)
    {
        m_CharacterControllerSystem->Update(*m_World, dt);
    }

    if (m_PlayerInputSystem && m_CharacterControllerSystem)
    {
        m_PlayerInputSystem->Update(*m_World, *m_CharacterControllerSystem, dt);
    }

    if (m_NavigationSystem && m_CharacterControllerSystem)
    {
        m_NavigationSystem->Update(*m_World, *m_CharacterControllerSystem, dt);
    }

    if (m_CharacterControllerSystem)
    {
        m_CharacterControllerSystem->RefreshTriggerOverlaps(*m_World);
    }

    if (m_ScriptSystem)
    {
        m_World->Each<TransformComponent, CameraComponent>(
            [this](Entity, TransformComponent& transform, CameraComponent& camera)
            {
                if (!camera.isActive)
                    return;

                ScriptSystem::CameraInfo cameraInfo;
                cameraInfo.posX = transform.position.x;
                cameraInfo.posY = transform.position.y;
                cameraInfo.posZ = transform.position.z;
                cameraInfo.fovDegrees = camera.fov;
                cameraInfo.nearZ = camera.nearPlane;
                cameraInfo.farZ = camera.farPlane;
                cameraInfo.renderMask = camera.renderMask;
                cameraInfo.enableViewmodelPass = camera.enableViewmodelPass;
                cameraInfo.viewmodelMask = camera.viewmodelMask;
                cameraInfo.viewmodelFov = camera.viewmodelFov;
                cameraInfo.viewmodelNearPlane = camera.viewmodelNearPlane;

                const float degToRad = 0.0174532925f;
                const float pitch = transform.rotation.x * degToRad;
                const float yaw = transform.rotation.y * degToRad;

                cameraInfo.forwardX = std::sin(yaw) * std::cos(pitch);
                cameraInfo.forwardY = -std::sin(pitch);
                cameraInfo.forwardZ = std::cos(yaw) * std::cos(pitch);

                const float rightYaw = yaw + 1.57079632679f;
                cameraInfo.rightX = std::sin(rightYaw);
                cameraInfo.rightY = 0.0f;
                cameraInfo.rightZ = std::cos(rightYaw);

                cameraInfo.upX = cameraInfo.forwardY * cameraInfo.rightZ - cameraInfo.forwardZ * cameraInfo.rightY;
                cameraInfo.upY = cameraInfo.forwardZ * cameraInfo.rightX - cameraInfo.forwardX * cameraInfo.rightZ;
                cameraInfo.upZ = cameraInfo.forwardX * cameraInfo.rightY - cameraInfo.forwardY * cameraInfo.rightX;

                m_ScriptSystem->SetCameraInfo(cameraInfo);
                if (m_UISystem)
                {
                    m_UISystem->SetCameraInfo(Vec3(cameraInfo.posX, cameraInfo.posY, cameraInfo.posZ),
                                             Vec3(cameraInfo.forwardX, cameraInfo.forwardY, cameraInfo.forwardZ),
                                             Vec3(cameraInfo.upX, cameraInfo.upY, cameraInfo.upZ), cameraInfo.fovDegrees,
                                             cameraInfo.nearZ, cameraInfo.farZ);
                }
            });

        m_ScriptSystem->Update(dt);
        m_ScriptSystem->PollFileChanges();
    }

    if (m_UISystem)
        m_UISystem->Tick(dt);
}

void SceneRuntime::SetCameraInfo(const ScriptSystem::CameraInfo& cameraInfo)
{
    if (m_ScriptSystem)
    {
        m_ScriptSystem->SetCameraInfo(cameraInfo);
    }
}

void SceneRuntime::SetViewportInfo(const ScriptSystem::ViewportInfo& viewportInfo)
{
    if (m_ScriptSystem)
    {
        m_ScriptSystem->SetViewportInfo(viewportInfo);
    }
    if (m_UISystem)
        m_UISystem->SetViewportSize(Vec2(viewportInfo.width, viewportInfo.height));
}

void SceneRuntime::SetMouseCaptured(bool captured)
{
    if (m_PlayerInputSystem)
    {
        m_PlayerInputSystem->SetMouseCaptured(captured);
    }
}

void SceneRuntime::SetScriptConsoleCallback(ScriptConsoleCallback callback)
{
    m_ScriptConsoleCallback = std::move(callback);

    if (m_ScriptSystem)
    {
        m_ScriptSystem->SetConsoleCallback(m_ScriptConsoleCallback);
    }
}

void SceneRuntime::SetStaticWorldGeometry(std::shared_ptr<StaticWorldGeometry> staticWorldGeometry)
{
    m_StaticWorldGeometry = std::move(staticWorldGeometry);

    if (m_ScriptSystem)
        m_ScriptSystem->SetStaticWorldGeometry(m_StaticWorldGeometry.get());
    if (m_CharacterControllerSystem)
        m_CharacterControllerSystem->SetStaticWorldGeometry(m_StaticWorldGeometry.get());
    if (m_NavigationSystem)
        m_NavigationSystem->SetStaticWorldGeometry(m_StaticWorldGeometry.get());
}

} // namespace Dot
