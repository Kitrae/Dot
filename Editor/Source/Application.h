// =============================================================================
// Dot Engine - Editor Application
// =============================================================================
// Main application class that manages window, RHI, and GUI lifecycle.
// =============================================================================

#pragma once

#include "Core/Core.h"

#include "Panels/AssetManagerPanel.h"
#include "Panels/DebugPanel.h"
#include "Panels/HierarchyPanel.h"
#include "Panels/InspectorPanel.h"
#include "Panels/MaterialGraphPanel.h"
#include "Panels/MapInspectorPanel.h"
#include "Panels/MapOutlinerPanel.h"
#include "Panels/PrefabViewerPanel.h"
#include "Panels/SceneSettingsPanel.h"
#include "Panels/TextEditorPanel.h"
#include "Panels/TextureViewerPanel.h"
#include "Panels/ViewportPanel.h"
#include "Core/Project/DotProjectAsset.h"
#include "Mcp/McpBridgeFwd.h"
#include "RHI/RHI.h"
#include "UI/ConfirmationDialog.h"
#include "Workspaces/Workspace.h"

#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>

#ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

namespace Dot
{

class SceneRuntime;
class LayoutWorkspace;
class MapWorkspace;
class UIWorkspace;
class MaterialWorkspace;
class ScriptingWorkspace;
class MapDocument;
class EditorSceneContext;
class SceneSerializer;
class GameExporter;

/// Editor application configuration
struct ApplicationConfig
{
    std::wstring Title = L"Dot Engine Editor";
    uint32_t Width = 1600;
    uint32_t Height = 900;
    bool VSync = true;
};

/// Play mode states
enum class PlayState : uint8
{
    Stopped = 0, // Editor mode - full editing
    Playing,     // Game running - panels hidden
    Paused       // Game paused - panels hidden, game frozen
};

/// Main Editor application class
class Application
{
public:
    Application(const ApplicationConfig& config = {});
    ~Application();

    // Non-copyable
    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;

    /// Initialize the application (window, RHI, GUI)
    bool Initialize(HINSTANCE hInstance);

    /// Run the main loop
    int Run();

    /// Request shutdown
    void RequestShutdown() { m_Running = false; }

    /// Check if shutting down
    bool IsRunning() const { return m_Running; }

    /// Play mode control
    void Play();  // Start or resume game
    void Pause(); // Pause game
    void Stop();  // Stop game & restore editor state
    bool IsPlaying() const { return m_PlayState == PlayState::Playing; }
    bool IsPaused() const { return m_PlayState == PlayState::Paused; }
    bool IsInPlayMode() const { return m_PlayState != PlayState::Stopped; }
    PlayState GetPlayState() const { return m_PlayState; }

    // Accessors
    HWND GetWindow() const { return m_Window; }
    RHIDevice* GetDevice() const { return m_Device.get(); }
    RHISwapChain* GetSwapChain() const { return m_SwapChain.get(); }
    RHIGUI* GetGUI() const { return m_GUI.get(); }

    /// Get singleton instance
    static Application* Get() { return s_Instance; }

private:
    bool CreateAppWindow(HINSTANCE hInstance);
    bool InitializeRHI();
    bool InitializeGUI();
    void Shutdown();

    void OnResize(uint32_t width, uint32_t height);

    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

private:
    ApplicationConfig m_Config;

    // Window
    HWND m_Window = nullptr;
    HINSTANCE m_Instance = nullptr;

    // RHI
    RHIDevicePtr m_Device;
    RHISwapChainPtr m_SwapChain;
    RHIGUIPtr m_GUI;

    // State
    bool m_Running = false;
    bool m_Minimized = false;

    // Play mode
    PlayState m_PlayState = PlayState::Stopped;
    std::string m_SavedSceneSnapshot; // JSON snapshot of scene before playing
    std::string m_SavedSceneSettingsSnapshot;
    float m_PlayTime = 0.0f;          // Elapsed time since play started

    // Panels
    std::unique_ptr<HierarchyPanel> m_HierarchyPanel;
    std::unique_ptr<InspectorPanel> m_InspectorPanel;
    std::unique_ptr<ViewportPanel> m_ViewportPanel;
    std::unique_ptr<class ConsolePanel> m_ConsolePanel;
    std::unique_ptr<DebugPanel> m_DebugPanel;
    std::unique_ptr<AssetManagerPanel> m_AssetManagerPanel;
    std::unique_ptr<TextEditorPanel> m_TextEditorPanel;
    std::unique_ptr<PrefabViewerPanel> m_PrefabViewerPanel;
    std::unique_ptr<TextureViewerPanel> m_TextureViewerPanel;
    std::unique_ptr<SceneSettingsPanel> m_SceneSettingsPanel;
    std::unique_ptr<MaterialGraphPanel> m_MaterialGraphPanel;
    std::unique_ptr<MapOutlinerPanel> m_MapOutlinerPanel;
    std::unique_ptr<MapInspectorPanel> m_MapInspectorPanel;
    std::unique_ptr<MapDocument> m_MapDocument;
    std::unique_ptr<MapDocument> m_SceneMapDocument;
    std::unique_ptr<EditorSceneContext> m_SceneContext;

    // Runtime scene execution path (physics/input/scripting during play mode)
    std::unique_ptr<SceneRuntime> m_SceneRuntime;

    // Singleton
    static Application* s_Instance;

    // Scene state
    ConfirmationDialog m_ConfirmationDialog;

    // Menu bar and toolbar
    void DrawMainMenuBar();
    void DrawToolbar();
    void DrawDebugVisOverlay();
    void NewScene();
    void OpenScene();
    void OpenScene(const std::filesystem::path& path);
    void OpenMap(const std::filesystem::path& path);
    void OpenUI(const std::filesystem::path& path);
    void QueueSceneOpen(const std::filesystem::path& path);
    void QueueMapOpen(const std::filesystem::path& path);
    void QueueUiOpen(const std::filesystem::path& path);
    bool SaveScene();
    bool SaveSceneAs();
    void RequestExit();
    void DrawModalDialogs();
    void DrawToolboxModal();
    void CreateFreshScene();
    bool LoadSceneFromPath(const std::filesystem::path& path);
    void ExecuteSceneActionWithSavePrompt(std::function<void()> action, const char* actionDescription);
    std::string CaptureSceneSnapshot() const;
    void CommitSceneSnapshotBaseline();
    bool HasUnsavedSceneChanges();
    void RefreshSceneDirtyState(float deltaTime, bool force = false);
    void SyncMapDocumentFromScene();
    void SyncSceneMapDocumentFromScene();
    void SyncSceneMapDocumentFromEditingDocument();
    bool SaveMapIfNeeded();
    std::filesystem::path ResolveDefaultMapPath() const;
    void EnsureSceneMapReference(const std::filesystem::path& mapPath);
    std::filesystem::path ResolveSceneSettingsPath(const std::filesystem::path& scenePath) const;
    bool SaveSceneSettings(const std::filesystem::path& scenePath);
    bool LoadSceneSettingsFromSerializer(const std::filesystem::path& scenePath, const SceneSerializer& serializer);
    void MigrateLegacySceneGlobalsToSceneSettings();

    // Update window title with scene name
    void UpdateWindowTitle();
    void OpenToolboxModal();
    void RunAutoReflectionProbeUtility();
    void ClearAutoReflectionProbeUtility();
    void OpenExportGameModal();
    void DrawExportGameModal();
    bool ExportGamePackage();
    std::filesystem::path GetEditorBinaryDirectory() const;
    std::filesystem::path ResolveRelativeScenePathInAssets() const;
    std::filesystem::path FindSourceShadersDirectory() const;
    void RebuildRuntimeFromToolbox();
    void SyncToolboxBindings();
    void EnsureWorkspaceSupported();
    void ProcessDeferredAssetActions();
    void ApplyToolboxChanges(bool notifyMcpTools = true);
    void RefreshMcpBridgeState();
    void HandleMcpBridgeRequest(const McpBridgeCommandRequest& request,
                                std::function<void(McpBridgeCommandResponse)> completion);
    std::filesystem::path GetProjectRootPath() const;

    // Workspace system
    void DrawWorkspaceTabs();
    void SwitchWorkspace(WorkspaceType type);
    WorkspaceType m_CurrentWorkspace = WorkspaceType::Layout;
    std::unique_ptr<LayoutWorkspace> m_LayoutWorkspace;
    std::unique_ptr<MapWorkspace> m_MapWorkspace;
    std::unique_ptr<UIWorkspace> m_UIWorkspace;
    std::unique_ptr<MaterialWorkspace> m_MaterialWorkspace;
    std::unique_ptr<ScriptingWorkspace> m_ScriptingWorkspace;
    bool m_RequestOpenToolboxModal = false;
    std::string m_ToolboxSelectedCategory = "All";
    std::string m_ToolboxUtilityStatusMessage;
    bool m_ToolboxUtilityStatusIsError = false;
    bool m_RequestOpenExportGameModal = false;
    struct ExportGameDialogState
    {
        std::string outputDirectory;
        DotProjectAsset projectAsset;
        bool cookDependenciesOnly = true;
        std::string statusMessage;
        bool statusIsError = false;
    } m_ExportGameDialog;

    enum class DeferredAssetActionType : uint8_t
    {
        None = 0,
        OpenScene,
        OpenMap,
        OpenUI,
    };

    DeferredAssetActionType m_DeferredAssetActionType = DeferredAssetActionType::None;
    std::optional<std::filesystem::path> m_DeferredAssetActionPath;
    std::unique_ptr<McpBridgeService> m_McpBridgeService;
};

} // namespace Dot
