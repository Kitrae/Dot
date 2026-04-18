// =============================================================================
// Dot Engine - Project Settings Metadata
// =============================================================================

#pragma once

#include "../Panels/SettingsPanel.h"

#include <array>
#include <string_view>

namespace Dot::ProjectSettingsMetadata
{

enum class SectionId
{
    EditorSnapping,
    GizmoGrid,
    GizmoAxisIndicator,
    GizmoViewport,
    GizmoNavigation,
    GizmoPhysics,
    ViewRendering,
    ViewRenderingDebug,
    PhysicsTimestep,
    PhysicsWorld,
    PhysicsSolver,
    CollisionDefaults,
    CollisionLayerSlots,
    CollisionMatrix
};

const std::array<SettingsPanel::Category, 5>& GetCategories();
const char* GetSidebarLabel(SettingsPanel::Category category);
const char* GetTitle(SettingsPanel::Category category);
const char* GetSubtitle(SettingsPanel::Category category);
size_t GetSectionCount(SettingsPanel::Category category);
SectionId GetSectionId(SettingsPanel::Category category, size_t index);
const char* GetSectionLabel(SectionId section);

bool MatchesCategory(SettingsPanel::Category category, std::string_view searchQuery);
bool MatchesSection(SettingsPanel::Category category, SectionId section, std::string_view searchQuery);

} // namespace Dot::ProjectSettingsMetadata
