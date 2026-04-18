// =============================================================================
// Dot Engine - Toolbox Manager
// =============================================================================

#include "ToolboxManager.h"

#include "ToolboxSettings.h"
#include "Core/Toolbox/ModuleIds.h"

#include <algorithm>

namespace Dot
{

using namespace ToolboxModuleIds;

void ToolboxManager::Initialize()
{
    PopulateDescriptors();
    LoadActiveStateFromSettings();
    ResetDraft();
    m_Initialized = true;
}

void ToolboxManager::Shutdown()
{
    m_ActiveStates.clear();
    m_DraftStates.clear();
    m_ActiveAdvancedView = false;
    m_DraftAdvancedView = false;
    m_Initialized = false;
}

void ToolboxManager::ResetDraft()
{
    if (!m_Initialized && m_ActiveStates.empty())
        Initialize();

    m_DraftStates = m_ActiveStates;
    m_DraftAdvancedView = m_ActiveAdvancedView;
}

void ToolboxManager::RestoreDraftDefaults()
{
    SetDefaults(m_DraftStates);
    ApplyDependencyRules(m_DraftStates);
    m_DraftAdvancedView = false;
}

void ToolboxManager::ApplyDraft()
{
    m_ActiveStates = m_DraftStates;
    m_ActiveAdvancedView = m_DraftAdvancedView;
    ApplyDependencyRules(m_ActiveStates);
    StoreActiveStateToSettings();
}

bool ToolboxManager::HasDraftChanges() const
{
    return m_DraftAdvancedView != m_ActiveAdvancedView || m_DraftStates != m_ActiveStates;
}

bool ToolboxManager::IsPhysicsEnabled() const
{
    return GetState(m_ActiveStates, kPhysics);
}

bool ToolboxManager::IsNavigationEnabled() const
{
    return GetState(m_ActiveStates, kNavigation);
}

bool ToolboxManager::IsPlayerInputEnabled() const
{
    return GetState(m_ActiveStates, kPlayerInput);
}

bool ToolboxManager::IsScriptingEnabled() const
{
    return GetState(m_ActiveStates, kScripting);
}

bool ToolboxManager::IsMapEditorEnabled() const
{
    return GetState(m_ActiveStates, kMapEditor);
}

bool ToolboxManager::IsUIEditorEnabled() const
{
    return GetState(m_ActiveStates, kUiEditor);
}

bool ToolboxManager::IsMaterialEditorEnabled() const
{
    return GetState(m_ActiveStates, kMaterialEditor);
}

bool ToolboxManager::IsNavMeshGizmoEnabled() const
{
    return GetState(m_ActiveStates, kNavMeshGizmo);
}

bool ToolboxManager::IsLightmapBakerEnabled() const
{
    return false;
}

bool ToolboxManager::IsMcpBridgeEnabled() const
{
    return GetState(m_ActiveStates, kMcpBridge);
}

bool ToolboxManager::IsWorkspaceEnabled(WorkspaceType type) const
{
    switch (type)
    {
        case WorkspaceType::Layout:
            return true;
        case WorkspaceType::Map:
            return IsMapEditorEnabled();
        case WorkspaceType::UI:
            return IsUIEditorEnabled();
        case WorkspaceType::Scripting:
            return IsScriptingEnabled();
        case WorkspaceType::Material:
            return IsMaterialEditorEnabled();
    }

    return true;
}

SceneRuntimeModuleConfig ToolboxManager::BuildSceneRuntimeConfig() const
{
    SceneRuntimeModuleConfig config;
    config.enablePhysics = IsPhysicsEnabled();
    config.enableNavigation = IsNavigationEnabled();
    config.enablePlayerInput = IsPlayerInputEnabled();
    config.enableScripting = IsScriptingEnabled();
    config.scriptFeatures.enablePhysicsBindings = GetState(m_ActiveStates, kScriptPhysicsBindings);
    config.scriptFeatures.enableNavigationBindings = GetState(m_ActiveStates, kScriptNavigationBindings);
    config.scriptFeatures.enablePerceptionBindings = GetState(m_ActiveStates, kScriptPerceptionBindings);
    return config;
}

bool ToolboxManager::IsModuleEnabled(std::string_view id) const
{
    return GetState(m_ActiveStates, id);
}

bool ToolboxManager::IsDraftModuleEnabled(std::string_view id) const
{
    return GetState(m_DraftStates, id);
}

void ToolboxManager::SetDraftModuleEnabled(std::string_view id, bool enabled)
{
    const ToolboxModuleDescriptor* descriptor = FindDescriptor(id);
    if (descriptor == nullptr)
        return;

    if (descriptor->alwaysEnabled)
    {
        SetState(m_DraftStates, id, true);
        return;
    }

    if (enabled)
        EnableWithDependencies(m_DraftStates, id);
    else
        SetState(m_DraftStates, id, false);

    ApplyDependencyRules(m_DraftStates);
}

std::vector<std::string> ToolboxManager::GetVisibleCategories(bool includeAdvanced) const
{
    std::vector<std::string> categories;
    categories.emplace_back("All");

    for (const ToolboxModuleDescriptor& descriptor : m_Modules)
    {
        if (!includeAdvanced && descriptor.advancedOnly)
            continue;
        if (std::find(categories.begin(), categories.end(), descriptor.category) == categories.end())
            categories.emplace_back(descriptor.category);
    }

    return categories;
}

std::vector<const ToolboxModuleDescriptor*> ToolboxManager::GetVisibleModulesForCategory(const std::string& category,
                                                                                         bool includeAdvanced) const
{
    std::vector<const ToolboxModuleDescriptor*> result;
    for (const ToolboxModuleDescriptor& descriptor : m_Modules)
    {
        if (!includeAdvanced && descriptor.advancedOnly)
            continue;
        if (category != "All" && category != descriptor.category)
            continue;
        result.push_back(&descriptor);
    }

    return result;
}

std::vector<const ToolboxModuleDescriptor*> ToolboxManager::GetDependents(std::string_view id) const
{
    std::vector<const ToolboxModuleDescriptor*> result;
    for (const ToolboxModuleDescriptor& descriptor : m_Modules)
    {
        if (std::find(descriptor.dependencies.begin(), descriptor.dependencies.end(), id) != descriptor.dependencies.end())
            result.push_back(&descriptor);
    }

    return result;
}

std::string ToolboxManager::GetDependencySummary(const ToolboxModuleDescriptor& descriptor) const
{
    if (descriptor.dependencies.empty())
        return {};

    std::string summary = "Requires ";
    for (size_t index = 0; index < descriptor.dependencies.size(); ++index)
    {
        const ToolboxModuleDescriptor* dependency = FindDescriptor(descriptor.dependencies[index]);
        summary += dependency != nullptr ? dependency->displayName : descriptor.dependencies[index];
        if (index + 1 < descriptor.dependencies.size())
            summary += ", ";
    }

    return summary;
}

const ToolboxModuleDescriptor* ToolboxManager::FindDescriptor(std::string_view id) const
{
    for (const ToolboxModuleDescriptor& descriptor : m_Modules)
    {
        if (descriptor.id == id)
            return &descriptor;
    }

    return nullptr;
}

bool ToolboxManager::GetState(const std::unordered_map<std::string, bool>& states, std::string_view id) const
{
    const auto iterator = states.find(std::string(id));
    if (iterator != states.end())
        return iterator->second;

    const ToolboxModuleDescriptor* descriptor = FindDescriptor(id);
    return descriptor != nullptr && descriptor->defaultEnabled;
}

void ToolboxManager::SetState(std::unordered_map<std::string, bool>& states, std::string_view id, bool enabled)
{
    states[std::string(id)] = enabled;
}

void ToolboxManager::PopulateDescriptors()
{
    if (!m_Modules.empty())
        return;

    m_Modules = {
        {kPhysics,
         "Physics",
         "Enables rigid bodies, colliders, character controllers, and physics-driven runtime systems.",
         "Runtime",
         "Runtime",
         true,
         false,
         false,
         {}},
        {kNavigation,
         "Navigation",
         "Builds and updates navigation systems for AI movement and navmesh-dependent runtime behavior.",
         "Runtime",
         "Runtime",
         true,
         false,
         false,
         {kPhysics}},
        {kPlayerInput,
         "Player Input",
         "Hooks gameplay input into the runtime character controller pipeline.",
         "Runtime",
         "Runtime",
         true,
         false,
         false,
         {kPhysics}},
        {kScripting,
         "Scripting",
         "Runs Lua gameplay scripts and exposes runtime script editing workflows.",
         "Runtime",
         "Runtime",
         true,
         false,
         false,
         {}},
        {kMapEditor,
         "Map Editor",
         "Unlocks the map authoring workspace and map asset editing flows in the editor.",
         "Workspaces",
         "Editor",
         true,
         false,
         false,
         {}},
        {kUiEditor,
         "UI Editor",
         "Unlocks the UI authoring workspace and .dotui asset editing flows in the editor.",
         "Workspaces",
         "Editor",
         true,
         false,
         false,
         {}},
        {kMaterialEditor,
         "Material Editor",
         "Unlocks material graph editing and preview tooling inside the editor.",
         "Workspaces",
         "Editor",
         true,
         false,
         false,
         {}},
        {kNavMeshGizmo,
         "NavMesh Gizmo",
         "Shows navigation debug overlays in the viewport when navigation tooling is available.",
         "Editor",
         "Debug",
         true,
         false,
         true,
         {kNavigation}},
        {kScriptPhysicsBindings,
         "Script Physics Bindings",
         "Exposes physics types and collision callbacks to Lua scripts.",
         "Scripting",
         "Bindings",
         true,
         false,
         true,
         {kScripting, kPhysics}},
        {kScriptNavigationBindings,
         "Script Navigation Bindings",
         "Exposes navigation queries and move requests to Lua scripts.",
         "Scripting",
         "Bindings",
         true,
         false,
         true,
         {kScripting, kNavigation}},
        {kScriptPerceptionBindings,
         "Script Perception Bindings",
         "Exposes runtime perception helpers that depend on the core gameplay simulation stack.",
         "Scripting",
         "Bindings",
         true,
         false,
         true,
         {kScripting, kPhysics}},
        {kMcpBridge,
         "MCP Bridge",
         "Exposes a local MCP bridge so AI clients can inspect and operate Toolbox-backed editor features.",
         "Integration",
         "Editor",
         false,
         false,
         false,
         {}},
    };
}

void ToolboxManager::LoadActiveStateFromSettings()
{
    const ToolboxSettings& settings = ToolboxSettings::Get();
    m_ActiveStates.clear();
    SetDefaults(m_ActiveStates);

    for (const auto& [moduleId, enabled] : settings.moduleStates)
        m_ActiveStates[moduleId] = enabled;

    ApplyDependencyRules(m_ActiveStates);
    m_ActiveAdvancedView = settings.showAdvancedModules;
    StoreActiveStateToSettings();
}

void ToolboxManager::StoreActiveStateToSettings() const
{
    ToolboxSettings& settings = ToolboxSettings::Get();
    settings.moduleStates.clear();
    for (const ToolboxModuleDescriptor& descriptor : m_Modules)
        settings.moduleStates[descriptor.id] = GetState(m_ActiveStates, descriptor.id);

    settings.showAdvancedModules = m_ActiveAdvancedView;
}

void ToolboxManager::ApplyDependencyRules(std::unordered_map<std::string, bool>& states) const
{
    bool changed = true;
    while (changed)
    {
        changed = false;
        for (const ToolboxModuleDescriptor& descriptor : m_Modules)
        {
            const bool isEnabled = GetState(states, descriptor.id);
            if (descriptor.alwaysEnabled && !isEnabled)
            {
                SetState(states, descriptor.id, true);
                changed = true;
                continue;
            }

            if (!isEnabled)
                continue;

            for (const std::string& dependencyId : descriptor.dependencies)
            {
                if (!GetState(states, dependencyId))
                {
                    SetState(states, descriptor.id, false);
                    changed = true;
                    break;
                }
            }
        }
    }
}

void ToolboxManager::EnableWithDependencies(std::unordered_map<std::string, bool>& states, std::string_view id)
{
    const ToolboxModuleDescriptor* descriptor = FindDescriptor(id);
    if (descriptor == nullptr)
        return;

    for (const std::string& dependencyId : descriptor->dependencies)
        EnableWithDependencies(states, dependencyId);

    SetState(states, id, true);
}

void ToolboxManager::SetDefaults(std::unordered_map<std::string, bool>& states) const
{
    states.clear();
    for (const ToolboxModuleDescriptor& descriptor : m_Modules)
        states[descriptor.id] = descriptor.defaultEnabled || descriptor.alwaysEnabled;
}

} // namespace Dot

