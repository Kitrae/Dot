// =============================================================================
// Dot Engine - Hierarchy Panel Implementation
// =============================================================================

#include "HierarchyPanel.h"
#include "PanelChrome.h"

#include "Core/ECS/World.h"
#include "Core/Scene/CameraComponent.h"
#include "Core/Scene/Components.h"
#include "Core/Scene/LightComponent.h"
#include "Core/Scene/MeshComponent.h"
#include "Core/Scene/PrefabComponent.h"
#include "Core/Scene/PrefabSystem.h"
#include "Core/Scene/ScriptComponent.h"

#include "../Commands/CommandRegistry.h"
#include "../Commands/EntityClipboard.h"
#include "../Commands/CreateEntityCommands.h"
#include "../Scene/EditorSceneContext.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <imgui.h>

// FontAwesome 6 icons (requires fa-solid-900.ttf font loaded)
#define ICON_FOLDER "\xef\x81\xbb "    // U+F07B folder
#define ICON_LIGHT "\xef\x83\xab "     // U+F0EB lightbulb
#define ICON_PRIMITIVE "\xef\x86\xb2 " // U+F1B2 cube
#define ICON_PREFAB "\xef\x86\xb6 "    // U+F1B6 building (prefab indicator)

namespace Dot
{

namespace
{

struct SearchToken
{
    std::string text;
    bool isKeyword = false;
};

void PushHierarchyPanelStyle()
{
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8.0f, 5.0f));
    ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.18f, 0.22f, 0.28f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.24f, 0.31f, 0.40f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.28f, 0.38f, 0.49f, 1.0f));
}

void PopHierarchyPanelStyle()
{
    ImGui::PopStyleColor(3);
    ImGui::PopStyleVar();
}

std::string ToLowerCopy(const char* text)
{
    if (!text)
        return {};

    std::string result(text);
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return result;
}

std::vector<SearchToken> TokenizeSearch(const char* search)
{
    std::vector<SearchToken> tokens;
    if (!search || search[0] == '\0')
        return tokens;

    std::string current;
    for (const char* it = search; *it; ++it)
    {
        const char ch = *it;
        if (std::isspace(static_cast<unsigned char>(ch)))
        {
            if (!current.empty())
            {
                tokens.push_back({ToLowerCopy(current.c_str()), false});
                current.clear();
            }
            continue;
        }

        current.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
    }

    if (!current.empty())
        tokens.push_back({ToLowerCopy(current.c_str()), false});

    return tokens;
}

bool ContainsSubstring(const std::string& haystack, const std::string& needle)
{
    return haystack.find(needle) != std::string::npos;
}

std::string GetEntityDisplayName(World* world, Entity entity)
{
    if (!world || !world->IsAlive(entity))
        return {};

    if (auto* name = world->GetComponent<NameComponent>(entity))
        return name->name;

    char buffer[64];
    snprintf(buffer, sizeof(buffer), "Entity %u", entity.GetIndex());
    return buffer;
}

bool MatchesCreateSearch(const CommandInfo& info, const char* search)
{
    if (!search || search[0] == '\0')
        return true;

    const std::string searchLower = ToLowerCopy(search);
    const std::string haystack = ToLowerCopy((info.name + " " + info.menuPath).c_str());
    return haystack.find(searchLower) != std::string::npos;
}

std::string GetCreateSectionName(const CommandInfo& info)
{
    static const std::string prefix = "Create/";
    std::string relativePath = info.menuPath;
    if (relativePath.find(prefix) == 0)
        relativePath = relativePath.substr(prefix.size());

    const size_t slashPos = relativePath.find('/');
    return (slashPos == std::string::npos) ? std::string("General") : relativePath.substr(0, slashPos);
}

std::string GetCreateEntryLabel(const CommandInfo& info)
{
    static const std::string prefix = "Create/";
    std::string relativePath = info.menuPath;
    if (relativePath.find(prefix) == 0)
        relativePath = relativePath.substr(prefix.size());
    return relativePath;
}

} // namespace

void HierarchyPanel::OnImGui()
{
    if (!m_Open)
        return;

    if (m_SceneContext)
        m_World = m_SceneContext->GetWorld();
    SyncSelectionFromContext();

    BeginChromeWindow(m_Name.c_str(), &m_Open);
    PushHierarchyPanelStyle();

    if (!m_World)
    {
        ImGui::TextDisabled("No world assigned");
        PopHierarchyPanelStyle();
        ImGui::End();
        return;
    }

    PruneSelection();

    ImGui::TextDisabled("SCENE");
    ImGui::SameLine();
    ImGui::Text("%zu entities", m_World->GetEntityCount());
    if (m_SelectedEntities.size() > 1)
    {
        ImGui::SameLine();
        ImGui::TextDisabled("| %zu selected", m_SelectedEntities.size());
    }
    ImGui::Separator();
    DrawFilterBar();
    ImGui::Separator();

    const bool hierarchyFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
    const bool renamingActive = m_RenamingEntity.IsValid();
    const bool canHandleShortcuts = hierarchyFocused && !renamingActive && !ImGui::IsAnyItemActive();
    const std::vector<Entity> actionSelection = GetActionSelection();
    const bool hasSelection = !actionSelection.empty();
    if (canHandleShortcuts && ImGui::GetIO().KeyCtrl)
    {
        EntityClipboard& clipboard = GetEntityClipboard();

        if (ImGui::IsKeyPressed(ImGuiKey_C, false) && hasSelection)
        {
            clipboard.entries = CreateClipboardEntries(*m_World, actionSelection);
            clipboard.hasData = true;
            clipboard.wasCut = false;
        }

        if (ImGui::IsKeyPressed(ImGuiKey_X, false) && hasSelection)
        {
            clipboard.entries = CreateClipboardEntries(*m_World, actionSelection);
            clipboard.hasData = true;
            clipboard.wasCut = true;

            auto cmd = std::make_unique<DeleteEntitiesCommand>(m_World, actionSelection, &m_SelectedEntity);
            CommandRegistry::Get().ExecuteCommand(std::move(cmd));
            ClearSelection();
        }

        if (ImGui::IsKeyPressed(ImGuiKey_V, false) && clipboard.hasData && !clipboard.entries.empty())
        {
            Entity pastedPrimary = kNullEntity;
            std::vector<Entity> pastedEntities;
            auto cmd = std::make_unique<PasteEntitiesCommand>(m_World, clipboard.entries, &pastedEntities,
                                                              &pastedPrimary, !clipboard.wasCut);
            CommandRegistry::Get().ExecuteCommand(std::move(cmd));
            if (pastedPrimary.IsValid())
            {
                SetSelectedEntities(pastedEntities, pastedPrimary);
                if (clipboard.wasCut)
                    clipboard = {};
            }
        }
    }

    if ((ImGui::IsKeyPressed(ImGuiKey_Delete) || ImGui::IsKeyPressed(ImGuiKey_Backspace)) && canHandleShortcuts &&
        hasSelection)
    {
        auto cmd = std::make_unique<DeleteEntitiesCommand>(m_World, actionSelection, &m_SelectedEntity);
        CommandRegistry::Get().ExecuteCommand(std::move(cmd));
        ClearSelection();
    }

    ImGui::BeginChild("HierarchyTree", ImVec2(0, -56.0f), true);
    m_VisibleEntitiesInOrder.clear();

    // Bind the create popup to the child region so right-click works inside the panel body.
    if (ImGui::BeginPopupContextWindow("HierarchyContext"))
    {
        if (ImGui::IsWindowAppearing())
        {
            m_CreateSearchBuffer[0] = '\0';
            ImGui::SetKeyboardFocusHere();
        }

        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputTextWithHint("##HierarchyCreateSearch", "Search scene nodes...", m_CreateSearchBuffer,
                                 sizeof(m_CreateSearchBuffer));
        ImGui::Spacing();

        const std::vector<const CommandInfo*> commands = CommandRegistry::Get().GetCommandsInCategory("Create");
        Entity createdEntity = kNullEntity;
        bool matchedAnything = false;
        std::vector<std::string> sections;
        sections.reserve(commands.size());
        for (const CommandInfo* info : commands)
        {
            if (!info)
                continue;

            const std::string sectionName = GetCreateSectionName(*info);
            if (std::find(sections.begin(), sections.end(), sectionName) == sections.end())
                sections.push_back(sectionName);
        }

        ImGui::BeginChild("HierarchyCreateList", ImVec2(0.0f, 240.0f), false);
        for (const std::string& section : sections)
        {
            bool sectionHasMatch = false;
            for (const CommandInfo* info : commands)
            {
                if (!info || !MatchesCreateSearch(*info, m_CreateSearchBuffer))
                    continue;

                if (GetCreateSectionName(*info) == section)
                {
                    sectionHasMatch = true;
                    break;
                }
            }

            if (!sectionHasMatch)
                continue;

            ImGui::TextDisabled("%s", section.c_str());
            for (const CommandInfo* info : commands)
            {
                if (!info || !MatchesCreateSearch(*info, m_CreateSearchBuffer))
                    continue;
                if (GetCreateSectionName(*info) != section)
                    continue;

                matchedAnything = true;
                const std::string label = GetCreateEntryLabel(*info);
                if (ImGui::MenuItem(label.c_str()))
                    CommandRegistry::Get().Execute(info->menuPath, m_World, &createdEntity);
            }
            ImGui::Separator();
        }

        if (!matchedAnything)
            ImGui::TextDisabled("No create actions match \"%s\"", m_CreateSearchBuffer);
        ImGui::EndChild();

        if (createdEntity.IsValid())
        {
            SelectSingleEntity(createdEntity);
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    int totalCount = 0;
    m_World->EachEntity(
        [this, &totalCount](Entity entity)
        {
            ++totalCount;
            auto* hierarchy = m_World->GetComponent<HierarchyComponent>(entity);
            if (!hierarchy || hierarchy->parent == kNullEntity)
                DrawEntityNode(entity);
        });

    if (totalCount == 0)
    {
        ImGui::Dummy(ImVec2(0.0f, 12.0f));
        ImGui::TextDisabled("Right-click to create your first node");
    }

    if (canHandleShortcuts && ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_A, false))
        SelectAllVisibleEntities();

    if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGui::IsAnyItemHovered())
    {
        ClearSelection();
    }

    ImGui::EndChild();

    ImVec2 availableSize = ImGui::GetContentRegionAvail();
    if (availableSize.x < 1.0f)
        availableSize.x = 1.0f;
    if (availableSize.y < 50.0f)
        availableSize.y = 50.0f;
    ImGui::InvisibleButton("##prefab_drop_zone", availableSize);

    if (ImGui::BeginDragDropTarget())
    {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("PREFAB_ASSET"))
        {
            const char* prefabPath = static_cast<const char*>(payload->Data);

            // Instantiate the prefab at origin
            Entity instance = PrefabSystem::InstantiateFromFile(*m_World, prefabPath);

            if (instance.IsValid())
            {
                SelectSingleEntity(instance);
            }
        }
        ImGui::EndDragDropTarget();
    }

    PopHierarchyPanelStyle();
    ImGui::End();

    // Apply any pending reparent operations AFTER UI drawing is complete
    // This prevents modifying the hierarchy while we're still iterating
    ApplyPendingReparent();
}

void HierarchyPanel::DrawFilterBar()
{
    ImGui::TextDisabled("Filter");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputTextWithHint("##HierarchySearch", "Search name, mesh, light, prefab, selected...", m_SearchBuffer,
                             sizeof(m_SearchBuffer));

    const auto drawToggle = [](const char* label, bool& value)
    {
        if (value)
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.28f, 0.38f, 0.49f, 1.0f));
        const bool pressed = ImGui::SmallButton(label);
        if (value)
            ImGui::PopStyleColor();
        if (pressed)
            value = !value;
        return pressed;
    };

    ImGui::Spacing();
    if (drawToggle("Selected", m_FilterSelected))
        ImGui::SameLine();
    else
        ImGui::SameLine();
    if (drawToggle("Lights", m_FilterLights))
        ImGui::SameLine();
    else
        ImGui::SameLine();
    if (drawToggle("Meshes", m_FilterMeshes))
        ImGui::SameLine();
    else
        ImGui::SameLine();
    if (drawToggle("Prefabs", m_FilterPrefabs))
        ImGui::SameLine();
    else
        ImGui::SameLine();

    if (ImGui::SmallButton("Clear"))
    {
        m_SearchBuffer[0] = '\0';
        m_FilterSelected = false;
        m_FilterLights = false;
        m_FilterMeshes = false;
        m_FilterPrefabs = false;
    }
}

void HierarchyPanel::DrawEntityNode(Entity entity)
{
    if (!m_World || !m_World->IsAlive(entity))
        return;

    const bool filtering = HasActiveFilters();
    if (filtering && !EntityOrDescendantMatchesFilters(entity))
        return;

    m_VisibleEntitiesInOrder.push_back(entity);

    // Get name or use fallback
    auto* name = m_World->GetComponent<NameComponent>(entity);
    const std::string displayNameString = GetEntityDisplayName(m_World, entity);
    const char* displayName = displayNameString.c_str();

    // Check if entity has children
    auto* hierarchy = m_World->GetComponent<HierarchyComponent>(entity);
    bool hasChildren = hierarchy && hierarchy->HasChildren();

    // Determine entity icon based on components
    const char* icon = "    ";
    bool isPrefabInstance = m_World->HasComponent<PrefabComponent>(entity);

    if (isPrefabInstance)
    {
        icon = ICON_PREFAB;
    }
    else if (m_World->HasComponent<DirectionalLightComponent>(entity) ||
             m_World->HasComponent<PointLightComponent>(entity) || m_World->HasComponent<SpotLightComponent>(entity))
    {
        icon = ICON_LIGHT;
    }
    else if (m_World->HasComponent<PrimitiveComponent>(entity))
    {
        icon = ICON_PRIMITIVE;
    }
    else
    {
        // No visual component = folder
        icon = ICON_FOLDER;
    }

    // Check if we are renaming this entity
    bool isRenaming = (m_RenamingEntity == entity);

    // Node flags
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
    if (!hasChildren)
        flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
    if (IsEntitySelected(entity))
        flags |= ImGuiTreeNodeFlags_Selected;

    if (filtering)
        ImGui::SetNextItemOpen(true, ImGuiCond_Always);

    // If renaming, use a special approach
    if (isRenaming)
    {
        // Draw tree node but immediately expand to show rename field
        flags |= ImGuiTreeNodeFlags_AllowOverlap;
        bool opened = ImGui::TreeNodeEx((void*)(uintptr_t)entity.id, flags, "%s", icon);

        ImGui::SameLine();

        // Set focus on first frame
        ImGui::SetKeyboardFocusHere();
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);

        if (ImGui::InputText("##rename", m_RenameBuffer, sizeof(m_RenameBuffer),
                             ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll))
        {
            // Enter pressed - apply rename
            if (name && m_RenameBuffer[0] != '\0')
            {
                name->name = m_RenameBuffer;
            }
            m_RenamingEntity = kNullEntity;
        }
        else if (ImGui::IsKeyPressed(ImGuiKey_Escape))
        {
            // Escape - cancel rename
            m_RenamingEntity = kNullEntity;
        }
        else if (!ImGui::IsItemActive() && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        {
            // Click away - apply rename
            if (name && m_RenameBuffer[0] != '\0')
            {
                name->name = m_RenameBuffer;
            }
            m_RenamingEntity = kNullEntity;
        }

        // Recursively draw children if opened
        if (opened)
        {
            // Re-fetch hierarchy in case it changed and make copy for safe iteration
            auto* hierarchyRefresh = m_World->GetComponent<HierarchyComponent>(entity);
            if (hierarchyRefresh)
            {
                std::vector<Entity> childrenCopy = hierarchyRefresh->children;
                for (Entity child : childrenCopy)
                {
                    if (m_World->IsAlive(child))
                        DrawEntityNode(child);
                }
            }
            if (hasChildren)
                ImGui::TreePop();
        }
        return;
    }

    // Normal display - draw node with icon
    bool opened = ImGui::TreeNodeEx((void*)(uintptr_t)entity.id, flags, "%s%s", icon, displayName);

    // Handle selection (single click)
    if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
    {
        const bool shift = ImGui::GetIO().KeyShift;
        const bool ctrl = ImGui::GetIO().KeyCtrl;

        if (shift)
            SelectRangeToEntity(entity, ctrl);
        else if (ctrl)
            ToggleEntitySelection(entity);
        else
            SelectSingleEntity(entity);
    }

    if (ImGui::IsItemClicked(ImGuiMouseButton_Right) && !IsEntitySelected(entity))
    {
        SelectSingleEntity(entity);
    }

    // Handle double-click to start renaming
    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
    {
        m_RenamingEntity = entity;
        if (name)
        {
            strncpy(m_RenameBuffer, name->name.c_str(), sizeof(m_RenameBuffer) - 1);
            m_RenameBuffer[sizeof(m_RenameBuffer) - 1] = '\0';
        }
        else
        {
            snprintf(m_RenameBuffer, sizeof(m_RenameBuffer), "Entity %u", entity.GetIndex());
        }
    }

    // Drag source - start dragging this entity
    if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None))
    {
        ImGui::SetDragDropPayload("ENTITY_REPARENT", &entity, sizeof(Entity));
        ImGui::Text("Reparent: %s", displayName);
        ImGui::EndDragDropSource();
    }

    // Drop target - make dropped entity a child of this one
    if (ImGui::BeginDragDropTarget())
    {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ENTITY_REPARENT"))
        {
            Entity droppedEntity = *(Entity*)payload->Data;
            if (droppedEntity != entity) // Can't parent to self
            {
                // Queue reparenting - don't apply immediately to avoid mid-iteration modification
                m_PendingReparent.entity = droppedEntity;
                m_PendingReparent.newParent = entity;
                m_PendingReparent.valid = true;
            }
        }
        ImGui::EndDragDropTarget();
    }

    // Right-click context menu
    if (ImGui::BeginPopupContextItem())
    {
        const std::vector<Entity> contextSelection = GetActionSelection(entity);
        EntityClipboard& clipboard = GetEntityClipboard();
        const bool hasContextSelection = !contextSelection.empty();
        const bool canPaste = clipboard.hasData && !clipboard.entries.empty();

        if (ImGui::MenuItem("Copy", "Ctrl+C", false, hasContextSelection))
        {
            clipboard.entries = CreateClipboardEntries(*m_World, contextSelection);
            clipboard.hasData = true;
            clipboard.wasCut = false;
        }
        if (ImGui::MenuItem("Cut", "Ctrl+X", false, hasContextSelection))
        {
            clipboard.entries = CreateClipboardEntries(*m_World, contextSelection);
            clipboard.hasData = true;
            clipboard.wasCut = true;

            auto cmd = std::make_unique<DeleteEntitiesCommand>(m_World, contextSelection, &m_SelectedEntity);
            CommandRegistry::Get().ExecuteCommand(std::move(cmd));
            ClearSelection();
        }
        if (ImGui::MenuItem("Paste", "Ctrl+V", false, canPaste))
        {
            Entity pastedPrimary = kNullEntity;
            std::vector<Entity> pastedEntities;
            auto cmd = std::make_unique<PasteEntitiesCommand>(m_World, clipboard.entries, &pastedEntities,
                                                              &pastedPrimary, !clipboard.wasCut);
            CommandRegistry::Get().ExecuteCommand(std::move(cmd));
            if (pastedPrimary.IsValid())
            {
                SetSelectedEntities(pastedEntities, pastedPrimary);
                if (clipboard.wasCut)
                    clipboard = {};
            }
        }

        ImGui::Separator();

        if (ImGui::MenuItem("Delete"))
        {
            auto cmd = std::make_unique<DeleteEntitiesCommand>(m_World, contextSelection, &m_SelectedEntity);
            CommandRegistry::Get().ExecuteCommand(std::move(cmd));
            ClearSelection();
        }
        if (ImGui::MenuItem("Duplicate"))
        {
            Entity duplicatedPrimary = kNullEntity;
            std::vector<Entity> duplicatedEntities;
            auto cmd = std::make_unique<PasteEntitiesCommand>(m_World, CreateClipboardEntries(*m_World, contextSelection),
                                                              &duplicatedEntities, &duplicatedPrimary, true);
            CommandRegistry::Get().ExecuteCommand(std::move(cmd));
            if (duplicatedPrimary.IsValid())
                SetSelectedEntities(duplicatedEntities, duplicatedPrimary);
        }
        if (hierarchy && hierarchy->parent != kNullEntity)
        {
            if (ImGui::MenuItem("Unparent"))
            {
                // Queue unparenting
                m_PendingReparent.entity = entity;
                m_PendingReparent.newParent = kNullEntity;
                m_PendingReparent.valid = true;
            }
        }

        ImGui::Separator();

        // Prefab operations
        if (ImGui::MenuItem("Create Prefab..."))
        {
            // Create prefab from this entity
            Prefab prefab = PrefabSystem::CreateFromEntity(*m_World, entity);

            // Create Prefabs directory if needed
            std::filesystem::create_directories("Assets/Prefabs");

            // Generate filename from entity name
            std::string filename = "Assets/Prefabs/" + prefab.GetName() + ".prefab";
            prefab.SaveToFile(filename);
        }

        // Show unpack option for prefab instances
        PrefabComponent* prefabComp = m_World->GetComponent<PrefabComponent>(entity);
        if (prefabComp && prefabComp->isRootInstance)
        {
            if (ImGui::MenuItem("Unpack Prefab"))
            {
                // Remove prefab component from this entity and all children
                m_World->RemoveComponent<PrefabComponent>(entity);
                if (hierarchy)
                {
                    for (Entity child : hierarchy->children)
                    {
                        if (m_World->HasComponent<PrefabComponent>(child))
                            m_World->RemoveComponent<PrefabComponent>(child);
                    }
                }
            }
        }

        ImGui::EndPopup();
    }

    // Recursively draw children if opened
    if (opened)
    {
        // Re-fetch hierarchy pointer in case drag-drop modified archetypes
        // and make a copy of children for safe iteration
        auto* hierarchyRefresh = m_World->GetComponent<HierarchyComponent>(entity);
        if (hierarchyRefresh && m_World->IsAlive(entity))
        {
            std::vector<Entity> childrenCopy = hierarchyRefresh->children;
            for (Entity child : childrenCopy)
            {
                if (m_World->IsAlive(child))
                    DrawEntityNode(child);
            }
        }
        if (hasChildren)
            ImGui::TreePop();
    }
}

void HierarchyPanel::SetEntityParent(Entity entity, Entity newParent)
{
    if (!m_World || !entity.IsValid())
        return;

    // Prevent parenting to self
    if (entity == newParent)
        return;

    auto* hierarchy = m_World->GetComponent<HierarchyComponent>(entity);
    if (!hierarchy)
        return;

    // Prevent circular parenting - check if newParent is a descendant of entity
    if (newParent.IsValid())
    {
        Entity check = newParent;
        while (check.IsValid() && m_World->IsAlive(check))
        {
            if (check == entity)
                return; // Would create circular reference
            auto* checkHierarchy = m_World->GetComponent<HierarchyComponent>(check);
            if (!checkHierarchy)
                break;
            check = checkHierarchy->parent;
        }
    }

    // Remove from old parent
    Entity oldParent = hierarchy->parent;
    if (oldParent.IsValid() && m_World->IsAlive(oldParent))
    {
        auto* oldParentHierarchy = m_World->GetComponent<HierarchyComponent>(oldParent);
        if (oldParentHierarchy)
            oldParentHierarchy->RemoveChild(entity);
    }

    // Set new parent
    hierarchy->parent = newParent;

    // Add to new parent's children
    if (newParent.IsValid() && m_World->IsAlive(newParent))
    {
        auto* newParentHierarchy = m_World->GetComponent<HierarchyComponent>(newParent);
        if (newParentHierarchy)
            newParentHierarchy->AddChild(entity);
    }
}

void HierarchyPanel::SetSelectedEntity(Entity entity)
{
    if (entity.IsValid())
        SelectSingleEntity(entity);
    else
        ClearSelection();
}

void HierarchyPanel::SelectSingleEntity(Entity entity)
{
    SetSelectedEntities(entity.IsValid() ? std::vector<Entity>{entity} : std::vector<Entity>{}, entity);
}

void HierarchyPanel::SetSelectedEntities(const std::vector<Entity>& entities, Entity primaryEntity)
{
    m_SelectedEntities.clear();

    std::vector<Entity> filteredEntities;
    filteredEntities.reserve(entities.size());
    for (Entity entity : entities)
    {
        if (!entity.IsValid() || !m_World || !m_World->IsAlive(entity))
            continue;
        if (std::find(filteredEntities.begin(), filteredEntities.end(), entity) != filteredEntities.end())
            continue;
        filteredEntities.push_back(entity);
    }

    m_SelectedEntities = std::move(filteredEntities);
    if (primaryEntity.IsValid() && IsEntitySelected(primaryEntity))
        m_SelectedEntity = primaryEntity;
    else
        m_SelectedEntity = m_SelectedEntities.empty() ? kNullEntity : m_SelectedEntities.back();
    m_SelectionAnchor = m_SelectedEntity;

    NotifySelectionChanged();
}

void HierarchyPanel::ToggleEntitySelection(Entity entity)
{
    if (!entity.IsValid())
        return;

    auto it = std::find(m_SelectedEntities.begin(), m_SelectedEntities.end(), entity);
    if (it != m_SelectedEntities.end())
    {
        const bool wasPrimary = (m_SelectedEntity == entity);
        m_SelectedEntities.erase(it);
        if (m_SelectedEntities.empty())
        {
            m_SelectedEntity = kNullEntity;
        }
        else if (wasPrimary)
        {
            m_SelectedEntity = m_SelectedEntities.back();
        }
    }
    else
    {
        m_SelectedEntities.push_back(entity);
        m_SelectedEntity = entity;
    }

    m_SelectionAnchor = entity;

    NotifySelectionChanged();
}

void HierarchyPanel::SelectRangeToEntity(Entity entity, bool additive)
{
    if (!entity.IsValid())
        return;

    Entity anchor = m_SelectionAnchor;
    if (!anchor.IsValid() || !m_World || !m_World->IsAlive(anchor))
        anchor = m_SelectedEntity;

    auto anchorIt = std::find(m_VisibleEntitiesInOrder.begin(), m_VisibleEntitiesInOrder.end(), anchor);
    auto entityIt = std::find(m_VisibleEntitiesInOrder.begin(), m_VisibleEntitiesInOrder.end(), entity);
    if (anchorIt == m_VisibleEntitiesInOrder.end() || entityIt == m_VisibleEntitiesInOrder.end())
    {
        SelectSingleEntity(entity);
        return;
    }

    const auto [rangeStart, rangeEnd] = std::minmax(anchorIt, entityIt);
    std::vector<Entity> nextSelection = additive ? m_SelectedEntities : std::vector<Entity>{};
    for (auto it = rangeStart; it != rangeEnd + 1; ++it)
    {
        if (std::find(nextSelection.begin(), nextSelection.end(), *it) == nextSelection.end())
            nextSelection.push_back(*it);
    }

    m_SelectedEntities = std::move(nextSelection);
    m_SelectedEntity = entity;
    m_SelectionAnchor = anchor;
    NotifySelectionChanged();
}

void HierarchyPanel::SelectAllVisibleEntities()
{
    SetSelectedEntities(m_VisibleEntitiesInOrder,
                        m_SelectedEntity.IsValid() ? m_SelectedEntity
                                                   : (m_VisibleEntitiesInOrder.empty() ? kNullEntity
                                                                                       : m_VisibleEntitiesInOrder.back()));
}

void HierarchyPanel::ClearSelection()
{
    m_SelectedEntity = kNullEntity;
    m_SelectionAnchor = kNullEntity;
    m_SelectedEntities.clear();
    NotifySelectionChanged();
}

void HierarchyPanel::PruneSelection()
{
    const Entity previousPrimary = m_SelectedEntity;
    m_SelectedEntities.erase(
        std::remove_if(m_SelectedEntities.begin(), m_SelectedEntities.end(),
                       [this](Entity entity) { return !entity.IsValid() || !m_World || !m_World->IsAlive(entity); }),
        m_SelectedEntities.end());

    if (m_SelectedEntities.empty() && m_SelectedEntity.IsValid() && m_World && m_World->IsAlive(m_SelectedEntity))
        m_SelectedEntities.push_back(m_SelectedEntity);

    if (!IsEntitySelected(m_SelectedEntity))
        m_SelectedEntity = m_SelectedEntities.empty() ? kNullEntity : m_SelectedEntities.back();

    if (m_SelectionAnchor.IsValid() && (!m_World || !m_World->IsAlive(m_SelectionAnchor)))
        m_SelectionAnchor = m_SelectedEntity;

    if (m_SelectedEntity != previousPrimary)
        NotifySelectionChanged();
}

bool HierarchyPanel::IsEntitySelected(Entity entity) const
{
    return std::find(m_SelectedEntities.begin(), m_SelectedEntities.end(), entity) != m_SelectedEntities.end();
}

void HierarchyPanel::NotifySelectionChanged()
{
    PublishSelectionToContext();
    if (m_OnSelectionChanged)
        m_OnSelectionChanged(m_SelectedEntity);
}

std::vector<Entity> HierarchyPanel::GetActionSelection(Entity contextEntity) const
{
    if (contextEntity.IsValid())
    {
        if (IsEntitySelected(contextEntity) && !m_SelectedEntities.empty())
            return m_SelectedEntities;
        return {contextEntity};
    }
    if (!m_SelectedEntities.empty())
        return m_SelectedEntities;
    if (m_SelectedEntity.IsValid())
        return {m_SelectedEntity};
    return {};
}

void HierarchyPanel::ApplyPendingReparent()
{
    if (!m_PendingReparent.valid)
        return;

    // Apply the deferred reparenting now that UI drawing is complete
    SetEntityParent(m_PendingReparent.entity, m_PendingReparent.newParent);

    // Clear the pending operation
    m_PendingReparent.valid = false;
    m_PendingReparent.entity = kNullEntity;
    m_PendingReparent.newParent = kNullEntity;
}

bool HierarchyPanel::HasActiveFilters() const
{
    return !TokenizeSearch(m_SearchBuffer).empty() || m_FilterSelected || m_FilterLights || m_FilterMeshes || m_FilterPrefabs;
}

bool HierarchyPanel::MatchesEntityFilters(Entity entity) const
{
    if (!m_World || !m_World->IsAlive(entity))
        return false;

    const bool searchActive = m_SearchBuffer[0] != '\0';
    const bool quickFiltersActive = m_FilterSelected || m_FilterLights || m_FilterMeshes || m_FilterPrefabs;

    bool quickMatches = true;
    if (quickFiltersActive)
    {
        quickMatches = false;
        if (m_FilterSelected && IsEntitySelected(entity))
            quickMatches = true;
        if (m_FilterLights &&
            (m_World->HasComponent<DirectionalLightComponent>(entity) ||
             m_World->HasComponent<PointLightComponent>(entity) ||
             m_World->HasComponent<SpotLightComponent>(entity)))
        {
            quickMatches = true;
        }
        if (m_FilterMeshes &&
            (m_World->HasComponent<MeshComponent>(entity) || m_World->HasComponent<PrimitiveComponent>(entity)))
        {
            quickMatches = true;
        }
        if (m_FilterPrefabs && m_World->HasComponent<PrefabComponent>(entity))
            quickMatches = true;
    }

    if (!searchActive)
        return quickMatches;

    const std::vector<SearchToken> tokens = TokenizeSearch(m_SearchBuffer);
    if (tokens.empty())
        return quickMatches;

    const std::string displayNameLower = ToLowerCopy(GetEntityDisplayName(m_World, entity).c_str());
    for (const SearchToken& token : tokens)
    {
        if (token.text.empty())
            continue;

        bool tokenMatched = false;
        if (token.text == "selected")
            tokenMatched = IsEntitySelected(entity);
        else if (token.text == "light")
        {
            tokenMatched = m_World->HasComponent<DirectionalLightComponent>(entity) ||
                           m_World->HasComponent<PointLightComponent>(entity) ||
                           m_World->HasComponent<SpotLightComponent>(entity);
        }
        else if (token.text == "mesh")
        {
            tokenMatched = m_World->HasComponent<MeshComponent>(entity) || m_World->HasComponent<PrimitiveComponent>(entity);
        }
        else if (token.text == "prefab")
        {
            tokenMatched = m_World->HasComponent<PrefabComponent>(entity);
        }
        else if (token.text == "camera")
        {
            tokenMatched = m_World->HasComponent<CameraComponent>(entity);
        }
        else if (token.text == "script")
        {
            tokenMatched = m_World->HasComponent<ScriptComponent>(entity);
        }
        else if (token.text == "primitive")
        {
            tokenMatched = m_World->HasComponent<PrimitiveComponent>(entity);
        }
        else if (token.text == "folder")
        {
            tokenMatched = !m_World->HasComponent<DirectionalLightComponent>(entity) &&
                           !m_World->HasComponent<PointLightComponent>(entity) &&
                           !m_World->HasComponent<SpotLightComponent>(entity) &&
                           !m_World->HasComponent<MeshComponent>(entity) &&
                           !m_World->HasComponent<PrimitiveComponent>(entity) &&
                           !m_World->HasComponent<PrefabComponent>(entity);
        }
        else
        {
            tokenMatched = ContainsSubstring(displayNameLower, token.text);
        }

        if (!tokenMatched)
            return false;
    }

    return quickMatches;
}

bool HierarchyPanel::EntityOrDescendantMatchesFilters(Entity entity) const
{
    if (MatchesEntityFilters(entity))
        return true;

    if (!m_World)
        return false;

    auto* hierarchy = m_World->GetComponent<HierarchyComponent>(entity);
    if (!hierarchy)
        return false;

    for (Entity child : hierarchy->children)
    {
        if (!m_World->IsAlive(child))
            continue;
        if (EntityOrDescendantMatchesFilters(child))
            return true;
    }

    return false;
}

void HierarchyPanel::SyncSelectionFromContext()
{
    if (!m_SceneContext)
        return;

    const EditorSelectionState& selectionState = m_SceneContext->GetEntitySelection();
    m_SelectedEntity = selectionState.GetPrimaryEntity();
    m_SelectedEntities = selectionState.GetEntities();
    if (m_SelectionAnchor.IsValid() && !IsEntitySelected(m_SelectionAnchor))
        m_SelectionAnchor = m_SelectedEntity;
}

void HierarchyPanel::PublishSelectionToContext()
{
    if (!m_SceneContext)
        return;
    m_SceneContext->GetEntitySelection().SetSelection(m_World, m_SelectedEntities, m_SelectedEntity);
}

} // namespace Dot
