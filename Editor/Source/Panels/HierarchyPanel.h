// =============================================================================
// Dot Engine - Hierarchy Panel
// =============================================================================
// Shows the scene tree with entities.
// =============================================================================

#pragma once

#include "Core/ECS/Entity.h"

#include "EditorPanel.h"

#include <vector>

namespace Dot
{

class World;
class EditorSceneContext;

/// Hierarchy panel - displays scene tree
class HierarchyPanel : public EditorPanel
{
public:
    HierarchyPanel() : EditorPanel("Hierarchy") {}

    void OnImGui() override;

    /// Set the ECS world to display
    void SetWorld(World* world) { m_World = world; }
    void SetSceneContext(EditorSceneContext* sceneContext) { m_SceneContext = sceneContext; }

    /// Get/set selected entity (synced with viewport)
    Entity GetSelectedEntity() const { return m_SelectedEntity; }
    const std::vector<Entity>& GetSelectedEntities() const { return m_SelectedEntities; }
    void SetSelectedEntity(Entity entity);

    /// Callback when selection changes
    using SelectionCallback = void (*)(Entity);
    void SetSelectionCallback(SelectionCallback callback) { m_OnSelectionChanged = callback; }

private:
    void DrawEntityNode(Entity entity);
    void SetEntityParent(Entity entity, Entity newParent);
    void SelectSingleEntity(Entity entity);
    void SetSelectedEntities(const std::vector<Entity>& entities, Entity primaryEntity);
    void ToggleEntitySelection(Entity entity);
    void SelectRangeToEntity(Entity entity, bool additive);
    void SelectAllVisibleEntities();
    void ClearSelection();
    void PruneSelection();
    bool IsEntitySelected(Entity entity) const;
    void NotifySelectionChanged();
    std::vector<Entity> GetActionSelection(Entity contextEntity = kNullEntity) const;
    void SyncSelectionFromContext();
    void PublishSelectionToContext();
    void DrawFilterBar();
    bool HasActiveFilters() const;
    bool MatchesEntityFilters(Entity entity) const;
    bool EntityOrDescendantMatchesFilters(Entity entity) const;

    World* m_World = nullptr;
    EditorSceneContext* m_SceneContext = nullptr;
    Entity m_SelectedEntity = kNullEntity;
    Entity m_SelectionAnchor = kNullEntity;
    std::vector<Entity> m_SelectedEntities;
    std::vector<Entity> m_VisibleEntitiesInOrder;
    SelectionCallback m_OnSelectionChanged = nullptr;

    // Inline rename state
    Entity m_RenamingEntity = kNullEntity;
    char m_RenameBuffer[128] = {};
    char m_CreateSearchBuffer[128] = {};
    char m_SearchBuffer[128] = {};
    bool m_FilterSelected = false;
    bool m_FilterLights = false;
    bool m_FilterMeshes = false;
    bool m_FilterPrefabs = false;

    // Deferred reparenting - applied after UI drawing to avoid mid-iteration crashes
    struct PendingReparent
    {
        Entity entity = kNullEntity;
        Entity newParent = kNullEntity;
        bool valid = false;
    };
    PendingReparent m_PendingReparent;

    void ApplyPendingReparent();
};

} // namespace Dot
