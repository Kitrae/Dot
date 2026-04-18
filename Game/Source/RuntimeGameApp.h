#pragma once

#include "RuntimeSceneRenderer.h"

#include <Core/Core.h>
#include <Core/ECS/World.h>
#include <Core/Map/MapTypes.h>
#include <Core/Scene/SceneRuntime.h>
#include <Core/Scene/SceneSettingsAsset.h>
#include <Core/Project/DotProjectAsset.h>
#include <RHI/RHI.h>
#include <RHI/RHIGUI.h>

#include <filesystem>
#include <memory>
#include <string>

#ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

namespace Dot
{

class SceneSerializer;
class StaticWorldGeometry;

class RuntimeGameApp
{
public:
    RuntimeGameApp();
    ~RuntimeGameApp();

    bool Initialize(HINSTANCE instance, const std::filesystem::path& projectPathOverride = {});
    int Run();

private:
    bool LoadProject(const std::filesystem::path& projectPathOverride);
    bool LoadScene();
    bool LoadSceneSettings(const SceneSerializer& serializer);
    void MigrateLegacySceneGlobalsToSceneSettings();
    bool LoadMap();
    bool CreateAppWindow();
    bool InitializeRHI();
    bool InitializeGUI();
    void Shutdown();
    void BuildRenderView(SceneRenderView& outView) const;
    bool HasActiveCamera() const;
    void FailStartup(const std::string& message);
    void UpdateWindowTitle();

    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    HINSTANCE m_Instance = nullptr;
    HWND m_Window = nullptr;
    bool m_Running = false;
    bool m_Minimized = false;

    std::filesystem::path m_ProjectPath;
    std::filesystem::path m_AssetsRoot;
    std::filesystem::path m_ShadersRoot;
    std::filesystem::path m_StartupScenePath;
    DotProjectAsset m_ProjectAsset;
    SceneSettingsAsset m_SceneSettings;
    MapAsset m_MapAsset;
    MapCompiledData m_MapCompiledData;
    World m_World;
    std::shared_ptr<StaticWorldGeometry> m_StaticWorldGeometry;

    std::unique_ptr<SceneRuntime> m_SceneRuntime;
    std::unique_ptr<RuntimeSceneRenderer> m_Renderer;

    RHIDevicePtr m_Device;
    RHISwapChainPtr m_SwapChain;
    RHIGUIPtr m_GUI;
};

} // namespace Dot
