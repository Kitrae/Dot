#include "RuntimeGameApp.h"

#include <Core/Assets/AssetManager.h>
#include <Core/Crash/CrashHandling.h>
#include <Core/Jobs/JobSystem.h>
#include <Core/Log.h>
#include <Core/Map/MapCompiler.h>
#include <Core/Map/MapSerializer.h>
#include <Core/Scene/AttachmentResolver.h>
#include <Core/Scene/LightComponent.h>
#include <Core/Scene/MapComponent.h>
#include <Core/Scene/MeshComponent.h>
#include <Core/Scene/ComponentReflection.h>
#include <Core/Scene/Components.h>
#include <Core/Scene/SceneSerializer.h>
#include <Core/Scene/SceneSettingsSerializer.h>
#include <Core/Scene/SkyboxComponent.h>

#include <chrono>

namespace Dot
{

namespace
{

constexpr const wchar_t* kWindowClassName = L"DotGameWindow";

std::wstring Utf8ToWide(const std::string& value)
{
    if (value.empty())
        return {};

    const int size = MultiByteToWideChar(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()), nullptr, 0);
    std::wstring result(static_cast<size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()), result.data(), size);
    return result;
}

bool IsLegacyOnlyEnvironmentEntity(World& world, Entity entity)
{
    size_t componentCount = 0;
    componentCount += world.HasComponent<NameComponent>(entity) ? 1u : 0u;
    componentCount += world.HasComponent<TransformComponent>(entity) ? 1u : 0u;
    componentCount += world.HasComponent<HierarchyComponent>(entity) ? 1u : 0u;
    componentCount += world.HasComponent<ActiveComponent>(entity) ? 1u : 0u;
    componentCount += world.HasComponent<SkyboxComponent>(entity) ? 1u : 0u;
    componentCount += world.HasComponent<AmbientLightComponent>(entity) ? 1u : 0u;
    componentCount += world.HasComponent<MapComponent>(entity) ? 1u : 0u;

    const bool hasOtherSceneComponents =
        world.HasComponent<PrimitiveComponent>(entity) || world.HasComponent<MeshComponent>(entity) ||
        world.HasComponent<CameraComponent>(entity) || world.HasComponent<DirectionalLightComponent>(entity) ||
        world.HasComponent<PointLightComponent>(entity) || world.HasComponent<SpotLightComponent>(entity) ||
        world.HasComponent<ScriptComponent>(entity);

    return !hasOtherSceneComponents && componentCount > 0;
}

} // namespace

RuntimeGameApp::RuntimeGameApp() = default;

RuntimeGameApp::~RuntimeGameApp()
{
    Shutdown();
}

bool RuntimeGameApp::Initialize(HINSTANCE instance, const std::filesystem::path& projectPathOverride)
{
    Log::Initialize("DotGame.log");
    MemorySystem::Get().Initialize();
    JobSystem::Get().Initialize();
    AssetManager::Get().Initialize();
    RegisterSceneComponents();

    m_Instance = instance;
    if (!LoadProject(projectPathOverride))
        return false;
    if (!CreateAppWindow())
        return false;
    if (!InitializeRHI())
        return false;
    if (!InitializeGUI())
        return false;
    AssetManager::Get().SetDevice(m_Device.get());
    if (!LoadScene())
        return false;
    if (!LoadMap())
        return false;

    m_Renderer = std::make_unique<RuntimeSceneRenderer>();
    m_Renderer->SetShaderRootPath(m_ShadersRoot);
    if (!m_Renderer->Initialize(m_Device.get()))
    {
        FailStartup("Failed to initialize runtime renderer.");
        return false;
    }
    m_Renderer->SetSceneSettings(m_SceneSettings);
    if (!m_MapCompiledData.IsEmpty())
        m_Renderer->SetCompiledMap(m_MapAsset, m_MapCompiledData);

    m_SceneRuntime = std::make_unique<SceneRuntime>();
    if (m_StaticWorldGeometry && m_SceneSettings.mapCollisionEnabled)
        m_SceneRuntime->SetStaticWorldGeometry(m_StaticWorldGeometry);
    if (!m_SceneRuntime->Initialize(&m_World))
    {
        FailStartup("Failed to initialize scene runtime.");
        return false;
    }
    m_SceneRuntime->Start();
    m_SceneRuntime->SetMouseCaptured(m_ProjectAsset.captureMouseOnStart);

    ResolveSceneTransforms(m_World);
    if (!HasActiveCamera())
    {
        FailStartup("Startup scene does not contain an active CameraComponent.");
        return false;
    }

    m_Running = true;
    UpdateWindowTitle();
    return true;
}

int RuntimeGameApp::Run()
{
    auto previous = std::chrono::high_resolution_clock::now();

    while (m_Running)
    {
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            if (msg.message == WM_QUIT)
            {
                m_Running = false;
                break;
            }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }

        if (!m_Running)
            break;
        if (m_Minimized)
            continue;

        auto now = std::chrono::high_resolution_clock::now();
        const float dt = std::chrono::duration<float>(now - previous).count();
        previous = now;

        MemorySystem::Get().BeginFrame();
        if (m_SceneRuntime)
        {
            ScriptSystem::ViewportInfo viewportInfo;
            viewportInfo.width = static_cast<float>(m_SwapChain->GetWidth());
            viewportInfo.height = static_cast<float>(m_SwapChain->GetHeight());
            m_SceneRuntime->SetViewportInfo(viewportInfo);
        }
        if (m_SceneRuntime)
            m_SceneRuntime->Tick(dt);

        ResolveSceneTransforms(m_World);

        m_Device->BeginFrame();
        m_SwapChain->SetClearColor(0.2f, 0.1f, 0.3f, 1.0f);
        m_SwapChain->BeginFrame();

        SceneRenderView renderView;
        BuildRenderView(renderView);
        if (m_Renderer)
            m_Renderer->Render(m_World, renderView, m_SwapChain.get());

        if (m_GUI && m_SceneRuntime && m_SceneRuntime->GetUISystem())
        {
            m_GUI->BeginFrame();
            m_SceneRuntime->GetUISystem()->RenderImGui();
            m_GUI->EndFrame();
        }

        m_SwapChain->EndFrame();
        m_Device->EndFrame();
        m_Device->Submit();
        m_SwapChain->Present();
        m_Device->Present();
        MemorySystem::Get().EndFrame();
    }

    return 0;
}

bool RuntimeGameApp::LoadProject(const std::filesystem::path& projectPathOverride)
{
    if (!projectPathOverride.empty())
    {
        m_ProjectPath = projectPathOverride;
    }
    else
    {
        wchar_t modulePath[MAX_PATH] = {};
        GetModuleFileNameW(nullptr, modulePath, MAX_PATH);
        m_ProjectPath = std::filesystem::path(modulePath).parent_path() / "Game.dotproject";
    }

    DotProjectSerializer serializer;
    if (!serializer.Load(m_ProjectAsset, m_ProjectPath))
    {
        FailStartup(serializer.GetLastError());
        return false;
    }

    m_AssetsRoot = m_ProjectPath.parent_path() / "Assets";
    m_ShadersRoot = m_ProjectPath.parent_path() / "Shaders";
    AssetManager::Get().SetRootPath(m_AssetsRoot.string());
    CrashHandling::SetProjectPath(m_ProjectPath.string());
    CrashHandling::SetRelaunchArguments("\"" + m_ProjectPath.string() + "\"");

    if (m_ProjectAsset.startupScene.empty())
    {
        FailStartup("Project file is missing startupScene.");
        return false;
    }

    if (!std::filesystem::exists(m_ShadersRoot))
    {
        FailStartup("Shaders directory does not exist: " + m_ShadersRoot.string());
        return false;
    }

    m_StartupScenePath = m_AssetsRoot / m_ProjectAsset.startupScene;
    if (!std::filesystem::exists(m_StartupScenePath))
    {
        FailStartup("Startup scene does not exist: " + m_StartupScenePath.string());
        return false;
    }

    CrashHandling::SetScenePath(m_StartupScenePath.string());

    return true;
}

bool RuntimeGameApp::LoadScene()
{
    SceneSerializer serializer;
    if (!serializer.Load(m_World, m_StartupScenePath.string()))
    {
        FailStartup(serializer.GetLastError());
        return false;
    }

    LoadSceneSettings(serializer);
    MigrateLegacySceneGlobalsToSceneSettings();
    return true;
}

bool RuntimeGameApp::LoadSceneSettings(const SceneSerializer& serializer)
{
    m_SceneSettings = SceneSettingsAsset{};
    const std::string& settingsReference = serializer.GetSceneSettingsReference();

    std::filesystem::path settingsPath;
    if (!settingsReference.empty())
        settingsPath = m_StartupScenePath.parent_path() / settingsReference;
    if (settingsPath.empty() || !std::filesystem::exists(settingsPath))
        settingsPath = std::filesystem::path(m_StartupScenePath.string() + ".settings.json");
    if (!std::filesystem::exists(settingsPath))
        return false;

    SceneSettingsSerializer settingsSerializer;
    if (!settingsSerializer.Load(m_SceneSettings, settingsPath))
    {
        DOT_LOG_WARN("Failed to load scene settings: %s", settingsSerializer.GetLastError().c_str());
        return false;
    }
    return true;
}

void RuntimeGameApp::MigrateLegacySceneGlobalsToSceneSettings()
{
    std::vector<Entity> entitiesToDestroy;

    m_World.EachEntity(
        [&](Entity entity)
        {
            bool removeEntity = false;
            if (SkyboxComponent* skybox = m_World.GetComponent<SkyboxComponent>(entity))
            {
                m_SceneSettings.cubemapPath = skybox->cubemapPath;
                m_SceneSettings.wrapMode = static_cast<int>(skybox->wrapMode);
                m_SceneSettings.tintR = skybox->tintR;
                m_SceneSettings.tintG = skybox->tintG;
                m_SceneSettings.tintB = skybox->tintB;
                m_SceneSettings.rotation = skybox->rotation;
                m_SceneSettings.showMarkers = skybox->showMarkers;
                m_SceneSettings.ambientEnabled = skybox->ambientEnabled;
                m_SceneSettings.ambientColorR = skybox->ambientColorR;
                m_SceneSettings.ambientColorG = skybox->ambientColorG;
                m_SceneSettings.ambientColorB = skybox->ambientColorB;
                m_SceneSettings.ambientIntensity = skybox->ambientIntensity;
                m_SceneSettings.sunEnabled = skybox->sunEnabled;
                m_SceneSettings.sunRotationX = skybox->sunRotationX;
                m_SceneSettings.sunRotationY = skybox->sunRotationY;
                m_SceneSettings.sunColorR = skybox->sunColorR;
                m_SceneSettings.sunColorG = skybox->sunColorG;
                m_SceneSettings.sunColorB = skybox->sunColorB;
                m_SceneSettings.sunIntensity = skybox->sunIntensity;
                m_SceneSettings.sunCastShadows = skybox->sunCastShadows;
                m_SceneSettings.sunShadowBias = skybox->sunShadowBias;
                m_SceneSettings.sunShadowDistance = skybox->sunShadowDistance;
                m_World.RemoveComponent<SkyboxComponent>(entity);
                removeEntity = true;
            }

            if (AmbientLightComponent* ambient = m_World.GetComponent<AmbientLightComponent>(entity))
            {
                m_SceneSettings.ambientEnabled = true;
                m_SceneSettings.ambientColorR = ambient->color.x;
                m_SceneSettings.ambientColorG = ambient->color.y;
                m_SceneSettings.ambientColorB = ambient->color.z;
                m_SceneSettings.ambientIntensity = ambient->intensity;
                m_World.RemoveComponent<AmbientLightComponent>(entity);
                removeEntity = true;
            }

            if (MapComponent* map = m_World.GetComponent<MapComponent>(entity))
            {
                m_SceneSettings.mapPath = map->mapPath;
                m_SceneSettings.mapVisible = map->visible;
                m_SceneSettings.mapCollisionEnabled = map->collisionEnabled;
                m_World.RemoveComponent<MapComponent>(entity);
                removeEntity = true;
            }

            if (removeEntity && IsLegacyOnlyEnvironmentEntity(m_World, entity))
                entitiesToDestroy.push_back(entity);
        });

    for (Entity entity : entitiesToDestroy)
    {
        if (m_World.IsAlive(entity))
            m_World.DestroyEntity(entity);
    }
}

bool RuntimeGameApp::LoadMap()
{
    m_MapAsset.Clear();
    m_MapCompiledData = MapCompiledData{};
    m_StaticWorldGeometry.reset();

    if (m_SceneSettings.mapPath.empty())
        return true;

    const std::filesystem::path mapPath = m_AssetsRoot / m_SceneSettings.mapPath;
    MapSerializer serializer;
    if (!serializer.Load(m_MapAsset, mapPath.string()))
    {
        FailStartup(serializer.GetLastError());
        return false;
    }

    m_MapCompiledData = MapCompiler::Compile(m_MapAsset, 1);
    m_StaticWorldGeometry = std::make_shared<StaticWorldGeometry>();
    m_StaticWorldGeometry->Build(m_MapCompiledData);
    return true;
}

bool RuntimeGameApp::CreateAppWindow()
{
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = m_Instance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = kWindowClassName;

    if (!RegisterClassExW(&wc))
        return false;

    const DWORD style = WS_OVERLAPPEDWINDOW;
    RECT rect = {0, 0, static_cast<LONG>(m_ProjectAsset.windowWidth), static_cast<LONG>(m_ProjectAsset.windowHeight)};
    AdjustWindowRect(&rect, style, FALSE);

    m_Window = CreateWindowExW(0, kWindowClassName, Utf8ToWide(m_ProjectAsset.gameName).c_str(), style, CW_USEDEFAULT,
                               CW_USEDEFAULT, rect.right - rect.left, rect.bottom - rect.top, nullptr, nullptr,
                               m_Instance, this);
    if (!m_Window)
        return false;

    ShowWindow(m_Window, m_ProjectAsset.startFullscreen ? SW_MAXIMIZE : SW_SHOW);
    UpdateWindow(m_Window);
    return true;
}

bool RuntimeGameApp::InitializeRHI()
{
    m_Device = CreateRHIDevice();
    if (!m_Device)
        return false;

    RHISwapChainDesc swapChainDesc;
    swapChainDesc.WindowHandle = m_Window;
    swapChainDesc.Width = m_ProjectAsset.windowWidth;
    swapChainDesc.Height = m_ProjectAsset.windowHeight;
    swapChainDesc.BufferCount = 2;
    swapChainDesc.VSync = true;

    m_SwapChain = CreateSwapChain(m_Device.get(), swapChainDesc);
    return m_SwapChain != nullptr;
}

bool RuntimeGameApp::InitializeGUI()
{
    RHIGUIDesc guiDesc;
    guiDesc.WindowHandle = m_Window;
    guiDesc.Width = m_ProjectAsset.windowWidth;
    guiDesc.Height = m_ProjectAsset.windowHeight;
    m_GUI = CreateGUI(m_Device.get(), m_SwapChain.get(), guiDesc);
    return m_GUI != nullptr;
}

void RuntimeGameApp::Shutdown()
{
    if (m_SceneRuntime)
    {
        m_SceneRuntime->Shutdown();
        m_SceneRuntime.reset();
    }

    if (m_Renderer)
    {
        m_Renderer->Shutdown();
        m_Renderer.reset();
    }

    m_GUI.reset();

    if (m_Device)
        m_Device->Present();

    m_SwapChain.reset();
    m_Device.reset();

    AssetManager::Get().Shutdown();
    JobSystem::Get().Shutdown();
    MemorySystem::Get().Shutdown();
    Log::ClearListeners();

    if (m_Window)
    {
        DestroyWindow(m_Window);
        UnregisterClassW(kWindowClassName, m_Instance);
        m_Window = nullptr;
    }
}

void RuntimeGameApp::BuildRenderView(SceneRenderView& outView) const
{
    ZeroMemory(&outView, sizeof(outView));
}

bool RuntimeGameApp::HasActiveCamera() const
{
    bool found = false;
    const_cast<World&>(m_World).Each<CameraComponent>(
        [&](Entity, CameraComponent& camera)
        {
            if (!found && camera.isActive)
                found = true;
        });
    return found;
}

void RuntimeGameApp::FailStartup(const std::string& message)
{
    DOT_LOG_ERROR("%s", message.c_str());
    MessageBoxA(nullptr, message.c_str(), "DotGame Startup Error", MB_OK | MB_ICONERROR);
}

void RuntimeGameApp::UpdateWindowTitle()
{
    if (m_Window)
        SetWindowTextW(m_Window, Utf8ToWide(m_ProjectAsset.gameName).c_str());
}

LRESULT CALLBACK RuntimeGameApp::WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    RuntimeGameApp* app = nullptr;
    if (msg == WM_NCCREATE)
    {
        CREATESTRUCTW* createStruct = reinterpret_cast<CREATESTRUCTW*>(lParam);
        app = static_cast<RuntimeGameApp*>(createStruct->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
    }
    else
    {
        app = reinterpret_cast<RuntimeGameApp*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }

    switch (msg)
    {
        case WM_SIZE:
            if (app)
            {
                app->m_Minimized = (wParam == SIZE_MINIMIZED);
                if (app->m_SwapChain && wParam != SIZE_MINIMIZED)
                {
                    const uint32 width = static_cast<uint32>(LOWORD(lParam));
                    const uint32 height = static_cast<uint32>(HIWORD(lParam));
                    if (width > 0 && height > 0)
                    {
                        app->m_SwapChain->Resize(width, height);
                        if (app->m_GUI)
                            app->m_GUI->OnResize(width, height);
                    }
                }
            }
            return 0;
        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        default:
            return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}

} // namespace Dot
