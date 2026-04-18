#pragma once

#include "Core/ECS/Entity.h"

#include <vector>

namespace Dot
{

class World;

class EditorSelectionState
{
public:
    Entity GetPrimaryEntity() const { return m_PrimaryEntity; }
    const std::vector<Entity>& GetEntities() const { return m_SelectedEntities; }

    void Clear();
    void SetSelection(World* world, const std::vector<Entity>& entities, Entity primaryEntity);
    void SetPrimaryEntity(World* world, Entity entity);
    bool IsSelected(Entity entity) const;

private:
    Entity m_PrimaryEntity = kNullEntity;
    std::vector<Entity> m_SelectedEntities;
};

} // namespace Dot
