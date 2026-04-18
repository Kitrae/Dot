#include "EditorSelectionState.h"

#include "Core/ECS/World.h"

#include <algorithm>

namespace Dot
{

void EditorSelectionState::Clear()
{
    m_PrimaryEntity = kNullEntity;
    m_SelectedEntities.clear();
}

void EditorSelectionState::SetSelection(World* world, const std::vector<Entity>& entities, Entity primaryEntity)
{
    m_SelectedEntities.clear();
    m_SelectedEntities.reserve(entities.size());

    for (Entity entity : entities)
    {
        if (!entity.IsValid() || !world || !world->IsAlive(entity))
            continue;
        if (std::find(m_SelectedEntities.begin(), m_SelectedEntities.end(), entity) != m_SelectedEntities.end())
            continue;
        m_SelectedEntities.push_back(entity);
    }

    if (primaryEntity.IsValid() &&
        std::find(m_SelectedEntities.begin(), m_SelectedEntities.end(), primaryEntity) != m_SelectedEntities.end())
    {
        m_PrimaryEntity = primaryEntity;
    }
    else
    {
        m_PrimaryEntity = m_SelectedEntities.empty() ? kNullEntity : m_SelectedEntities.back();
    }
}

void EditorSelectionState::SetPrimaryEntity(World* world, Entity entity)
{
    SetSelection(world, entity.IsValid() ? std::vector<Entity>{entity} : std::vector<Entity>{}, entity);
}

bool EditorSelectionState::IsSelected(Entity entity) const
{
    return std::find(m_SelectedEntities.begin(), m_SelectedEntities.end(), entity) != m_SelectedEntities.end();
}

} // namespace Dot
