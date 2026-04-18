// =============================================================================
// Dot Engine - Project Settings Metadata
// =============================================================================

#include "ProjectSettingsMetadata.h"

#include <array>
#include <string>

namespace Dot::ProjectSettingsMetadata
{

namespace
{

struct TermList
{
    const char* const* values = nullptr;
    size_t count = 0;
};

struct SectionMetadata
{
    SectionId id;
    const char* label = nullptr;
    TermList keywords;
};

struct CategoryMetadata
{
    SettingsPanel::Category category = SettingsPanel::Category::Editor;
    const char* sidebarLabel = nullptr;
    const char* title = nullptr;
    const char* subtitle = nullptr;
    TermList keywords;
    const SectionMetadata* sections = nullptr;
    size_t sectionCount = 0;
};

std::string ToLowerCopy(std::string_view text)
{
    std::string result(text.begin(), text.end());
    for (char& c : result)
    {
        if (c >= 'A' && c <= 'Z')
            c = static_cast<char>(c - 'A' + 'a');
    }
    return result;
}

bool ContainsInsensitive(std::string_view haystack, std::string_view needle)
{
    if (needle.empty())
        return true;

    const std::string lowerHaystack = ToLowerCopy(haystack);
    const std::string lowerNeedle = ToLowerCopy(needle);
    return lowerHaystack.find(lowerNeedle) != std::string::npos;
}

bool MatchesTerms(const TermList& terms, std::string_view searchQuery)
{
    if (searchQuery.empty())
        return true;

    for (size_t i = 0; i < terms.count; ++i)
    {
        const char* term = terms.values[i];
        if (term && ContainsInsensitive(term, searchQuery))
            return true;
    }

    return false;
}

constexpr const char* kEditorCategoryKeywords[] = {"Editor", "Viewport", "Snapping"};
constexpr const char* kEditorSnappingKeywords[] = {"Snapping",      "Snap to Grid",        "Layout Translation Snap",
                                                   "Layout Move Step", "Layout Rotation Snap", "Layout Rotate Step",
                                                   "Layout Scale Snap", "Layout Scale Step",   "Map Grid Snap",
                                                   "Map Snap Step"};

constexpr const char* kGizmoCategoryKeywords[] = {"Gizmos", "Debug View", "Overlays", "Grid", "Axis", "Colliders",
                                                   "Camera Frustums", "Lights", "Viewport Gizmos", "Sockets",
                                                   "Attachment Sockets"};
constexpr const char* kGizmoGridKeywords[] = {"Grid", "Show Grid", "Grid Size", "Grid Spacing", "Grid Color"};
constexpr const char* kGizmoAxisKeywords[] = {"Axis Indicator", "Show Axis Indicator", "Axis Length"};
constexpr const char* kGizmoViewportKeywords[] = {"Viewport Gizmos", "Selection Gizmo", "Light Gizmos",
                                                  "Camera Frustums", "Transform Gizmo", "Attachment Sockets", "Sockets"};
constexpr const char* kGizmoNavigationKeywords[] = {"Navigation", "NavMesh", "Show NavMesh", "Navigation Debug"};
constexpr const char* kGizmoPhysicsKeywords[] = {"Physics Gizmos", "Show Colliders", "Show Contact Points"};

constexpr const char* kViewCategoryKeywords[] = {"View", "Viewport", "Rendering", "Debug Visualization"};
constexpr const char* kViewRenderingKeywords[] = {"Rendering", "Render Mode", "Wireframe", "Depth Buffer"};
constexpr const char* kViewRenderingDebugKeywords[] = {"Rendering Debug", "SSAO", "Shadows", "Anti-Aliasing",
                                                       "HZB", "Forward+", "Occlusion", "Ambient Occlusion",
                                                       "Shader", "Blur", "Half Resolution", "LOD", "Level Of Detail"};

constexpr const char* kPhysicsCategoryKeywords[] = {"Physics", "Simulation", "Gravity", "Solver"};
constexpr const char* kPhysicsTimestepKeywords[] = {"Timestep", "Simulation Rate", "Max Substeps"};
constexpr const char* kPhysicsWorldKeywords[] = {"World", "Gravity", "Earth", "Moon", "Zero-G"};
constexpr const char* kPhysicsSolverKeywords[] = {"Solver", "Position Correction", "Penetration Slop"};

constexpr const char* kCollisionCategoryKeywords[] = {
    "Collision Layers", "Collision Layer", "Layer Slots", "Collision Matrix", "Project-wide layer interaction"};
constexpr const char* kCollisionDefaultsKeywords[] = {"Defaults", "Reset Collision Defaults"};
constexpr const char* kCollisionLayerSlotsKeywords[] = {"Layer Slots", "Layer Name", "Collision Layer", "Player",
                                                        "World", "Trigger", "Enemy", "Pickup", "Projectile"};
constexpr const char* kCollisionMatrixKeywords[] = {"Collision Matrix", "Layer interaction", "Project-wide layer interaction"};

constexpr SectionMetadata kEditorSections[] = {
    {SectionId::EditorSnapping, "Snapping", {kEditorSnappingKeywords, std::size(kEditorSnappingKeywords)}},
};

constexpr SectionMetadata kGizmoSections[] = {
    {SectionId::GizmoGrid, "Grid", {kGizmoGridKeywords, std::size(kGizmoGridKeywords)}},
    {SectionId::GizmoAxisIndicator, "Axis Indicator", {kGizmoAxisKeywords, std::size(kGizmoAxisKeywords)}},
    {SectionId::GizmoViewport, "Viewport Gizmos", {kGizmoViewportKeywords, std::size(kGizmoViewportKeywords)}},
    {SectionId::GizmoNavigation, "Navigation", {kGizmoNavigationKeywords, std::size(kGizmoNavigationKeywords)}},
    {SectionId::GizmoPhysics, "Physics Debug", {kGizmoPhysicsKeywords, std::size(kGizmoPhysicsKeywords)}},
};

constexpr SectionMetadata kViewSections[] = {
    {SectionId::ViewRendering, "Rendering", {kViewRenderingKeywords, std::size(kViewRenderingKeywords)}},
    {SectionId::ViewRenderingDebug, "Rendering Debug",
     {kViewRenderingDebugKeywords, std::size(kViewRenderingDebugKeywords)}},
};

constexpr SectionMetadata kPhysicsSections[] = {
    {SectionId::PhysicsTimestep, "Timestep", {kPhysicsTimestepKeywords, std::size(kPhysicsTimestepKeywords)}},
    {SectionId::PhysicsWorld, "World", {kPhysicsWorldKeywords, std::size(kPhysicsWorldKeywords)}},
    {SectionId::PhysicsSolver, "Solver", {kPhysicsSolverKeywords, std::size(kPhysicsSolverKeywords)}},
};

constexpr SectionMetadata kCollisionSections[] = {
    {SectionId::CollisionDefaults, "Defaults", {kCollisionDefaultsKeywords, std::size(kCollisionDefaultsKeywords)}},
    {SectionId::CollisionLayerSlots, "Layer Slots", {kCollisionLayerSlotsKeywords, std::size(kCollisionLayerSlotsKeywords)}},
    {SectionId::CollisionMatrix, "Collision Matrix", {kCollisionMatrixKeywords, std::size(kCollisionMatrixKeywords)}},
};

constexpr CategoryMetadata kCategoryMetadata[] = {
    {SettingsPanel::Category::Editor, "Editor", "Editor", "Editor workflow defaults.",
     {kEditorCategoryKeywords, std::size(kEditorCategoryKeywords)}, kEditorSections, std::size(kEditorSections)},
    {SettingsPanel::Category::Gizmos, "Gizmos", "Gizmos", "Viewport overlays and debug helper visibility.",
     {kGizmoCategoryKeywords, std::size(kGizmoCategoryKeywords)}, kGizmoSections, std::size(kGizmoSections)},
    {SettingsPanel::Category::View, "View", "View", "Viewport rendering defaults, feature switches, and debug controls.",
     {kViewCategoryKeywords, std::size(kViewCategoryKeywords)}, kViewSections, std::size(kViewSections)},
    {SettingsPanel::Category::Physics, "Physics", "Physics", "Simulation and debug defaults for the runtime world.",
     {kPhysicsCategoryKeywords, std::size(kPhysicsCategoryKeywords)}, kPhysicsSections, std::size(kPhysicsSections)},
    {SettingsPanel::Category::CollisionLayers, "Collision Layers", "Collision Layers",
     "Project-wide names and filtering rules for colliders, queries, and controllers.",
     {kCollisionCategoryKeywords, std::size(kCollisionCategoryKeywords)}, kCollisionSections,
     std::size(kCollisionSections)},
};

const CategoryMetadata* FindMetadata(SettingsPanel::Category category)
{
    for (const CategoryMetadata& metadata : kCategoryMetadata)
    {
        if (metadata.category == category)
            return &metadata;
    }

    return nullptr;
}

const SectionMetadata* FindSectionMetadata(SettingsPanel::Category category, SectionId sectionId)
{
    const CategoryMetadata* metadata = FindMetadata(category);
    if (!metadata)
        return nullptr;

    for (size_t i = 0; i < metadata->sectionCount; ++i)
    {
        if (metadata->sections[i].id == sectionId)
            return &metadata->sections[i];
    }

    return nullptr;
}

} // namespace

const std::array<SettingsPanel::Category, 5>& GetCategories()
{
    static const std::array<SettingsPanel::Category, 5> categories = {
        SettingsPanel::Category::Editor,
        SettingsPanel::Category::Gizmos,
        SettingsPanel::Category::View,
        SettingsPanel::Category::Physics,
        SettingsPanel::Category::CollisionLayers,
    };
    return categories;
}

const char* GetSidebarLabel(SettingsPanel::Category category)
{
    const CategoryMetadata* metadata = FindMetadata(category);
    return metadata ? metadata->sidebarLabel : "Unknown";
}

const char* GetTitle(SettingsPanel::Category category)
{
    const CategoryMetadata* metadata = FindMetadata(category);
    return metadata ? metadata->title : "Unknown";
}

const char* GetSubtitle(SettingsPanel::Category category)
{
    const CategoryMetadata* metadata = FindMetadata(category);
    return metadata ? metadata->subtitle : "";
}

size_t GetSectionCount(SettingsPanel::Category category)
{
    const CategoryMetadata* metadata = FindMetadata(category);
    return metadata ? metadata->sectionCount : 0;
}

SectionId GetSectionId(SettingsPanel::Category category, size_t index)
{
    const CategoryMetadata* metadata = FindMetadata(category);
    if (!metadata || index >= metadata->sectionCount)
        return SectionId::EditorSnapping;

    return metadata->sections[index].id;
}

const char* GetSectionLabel(SectionId section)
{
    for (const CategoryMetadata& category : kCategoryMetadata)
    {
        for (size_t i = 0; i < category.sectionCount; ++i)
        {
            if (category.sections[i].id == section)
                return category.sections[i].label;
        }
    }

    return "Unknown";
}

bool MatchesCategory(SettingsPanel::Category category, std::string_view searchQuery)
{
    const CategoryMetadata* metadata = FindMetadata(category);
    if (!metadata)
        return false;

    if (MatchesTerms(metadata->keywords, searchQuery) || ContainsInsensitive(metadata->sidebarLabel, searchQuery) ||
        ContainsInsensitive(metadata->title, searchQuery))
    {
        return true;
    }

    for (size_t i = 0; i < metadata->sectionCount; ++i)
    {
        if (MatchesSection(category, metadata->sections[i].id, searchQuery))
            return true;
    }

    return false;
}

bool MatchesSection(SettingsPanel::Category category, SectionId section, std::string_view searchQuery)
{
    const SectionMetadata* metadata = FindSectionMetadata(category, section);
    if (!metadata)
        return false;

    if (searchQuery.empty())
        return true;

    return MatchesTerms(metadata->keywords, searchQuery) || ContainsInsensitive(metadata->label, searchQuery);
}

} // namespace Dot::ProjectSettingsMetadata
