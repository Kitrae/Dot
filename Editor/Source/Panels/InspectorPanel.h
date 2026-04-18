// =============================================================================
// Dot Engine - Inspector Panel
// =============================================================================
// Shows properties of the selected entity.
// =============================================================================

#pragma once

#include "Core/ECS/Entity.h"

#include "../Commands/CreateEntityCommands.h"
#include "EditorPanel.h"

#include <array>
#include <functional>

namespace Dot
{

class World;

/// Inspector panel - displays entity properties
class InspectorPanel : public EditorPanel
{
public:
    using SelectionCallback = std::function<void(Entity)>;
    using ReflectionProbeBakeCallback = std::function<void(Entity)>;

    InspectorPanel() : EditorPanel("Inspector") {}

    void OnImGui() override;

    /// Set the context for inspection
    void SetContext(World* world, Entity entity)
    {
        m_World = world;
        m_SelectedEntity = entity;
    }

    void SetSelectionCallback(SelectionCallback callback) { m_OnSelectionChanged = std::move(callback); }
    void SetReflectionProbeBakeCallback(ReflectionProbeBakeCallback callback)
    {
        m_OnBakeReflectionProbe = std::move(callback);
    }

private:
    void ApplyFbxMaterial(const std::string& meshPath);

    World* m_World = nullptr;
    Entity m_SelectedEntity = kNullEntity;
    Entity m_EditBaselineEntity = kNullEntity;
    EntityComponentSnapshot m_EditBaselineSnapshot;
    bool m_HasEditBaseline = false;
    std::array<char, 128> m_AttachmentTargetSearch = {};
    std::array<char, 128> m_AttachmentSocketSearch = {};
    SelectionCallback m_OnSelectionChanged = nullptr;
    ReflectionProbeBakeCallback m_OnBakeReflectionProbe = nullptr;
};

} // namespace Dot
