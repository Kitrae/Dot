// =============================================================================
// Dot Engine - Toolbox Manager
// =============================================================================

#pragma once

#include "Core/Scene/SceneRuntime.h"
#include "../Workspaces/Workspace.h"

#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace Dot
{

struct ToolboxModuleDescriptor
{
    const char* id = "";
    const char* displayName = "";
    const char* description = "";
    const char* category = "";
    const char* bundle = "";
    bool defaultEnabled = false;
    bool alwaysEnabled = false;
    bool advancedOnly = false;
    std::vector<std::string> dependencies;
};

class ToolboxManager
{
public:
    static ToolboxManager& Get()
    {
        static ToolboxManager instance;
        return instance;
    }

    void Initialize();
    void Shutdown();

    void ResetDraft();
    void RestoreDraftDefaults();
    void ApplyDraft();

    bool HasDraftChanges() const;

    bool IsPhysicsEnabled() const;
    bool IsNavigationEnabled() const;
    bool IsPlayerInputEnabled() const;
    bool IsScriptingEnabled() const;
    bool IsMapEditorEnabled() const;
    bool IsUIEditorEnabled() const;
    bool IsMaterialEditorEnabled() const;
    bool IsNavMeshGizmoEnabled() const;
    bool IsLightmapBakerEnabled() const;
    bool IsMcpBridgeEnabled() const;

    bool IsWorkspaceEnabled(WorkspaceType type) const;
    SceneRuntimeModuleConfig BuildSceneRuntimeConfig() const;

    bool IsModuleEnabled(std::string_view id) const;
    bool IsDraftModuleEnabled(std::string_view id) const;
    void SetDraftModuleEnabled(std::string_view id, bool enabled);

    bool IsDraftAdvancedViewEnabled() const { return m_DraftAdvancedView; }
    void SetDraftAdvancedView(bool enabled) { m_DraftAdvancedView = enabled; }

    std::vector<std::string> GetVisibleCategories(bool includeAdvanced) const;
    std::vector<const ToolboxModuleDescriptor*> GetVisibleModulesForCategory(const std::string& category,
                                                                             bool includeAdvanced) const;
    std::vector<const ToolboxModuleDescriptor*> GetDependents(std::string_view id) const;
    std::string GetDependencySummary(const ToolboxModuleDescriptor& descriptor) const;
    const std::vector<ToolboxModuleDescriptor>& GetModules() const { return m_Modules; }

private:
    ToolboxManager() = default;

    const ToolboxModuleDescriptor* FindDescriptor(std::string_view id) const;
    bool GetState(const std::unordered_map<std::string, bool>& states, std::string_view id) const;
    static void SetState(std::unordered_map<std::string, bool>& states, std::string_view id, bool enabled);
    void PopulateDescriptors();
    void LoadActiveStateFromSettings();
    void StoreActiveStateToSettings() const;
    void ApplyDependencyRules(std::unordered_map<std::string, bool>& states) const;
    void EnableWithDependencies(std::unordered_map<std::string, bool>& states, std::string_view id);
    void SetDefaults(std::unordered_map<std::string, bool>& states) const;

    std::vector<ToolboxModuleDescriptor> m_Modules;
    std::unordered_map<std::string, bool> m_ActiveStates;
    std::unordered_map<std::string, bool> m_DraftStates;
    bool m_ActiveAdvancedView = false;
    bool m_DraftAdvancedView = false;
    bool m_Initialized = false;
};

} // namespace Dot

