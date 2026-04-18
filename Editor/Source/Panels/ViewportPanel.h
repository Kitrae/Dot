// =============================================================================
// Dot Engine - Viewport Panel
// =============================================================================
// 3D scene viewport for rendering.
// =============================================================================

#pragma once

#include "Core/ECS/World.h"
#include "Core/Material/MaterialLoader.h"
#include "Core/Scene/Components.h"

#include "../Map/MapDocument.h"
#include "../Gizmos/RayPicker.h"
#include "../Gizmos/RotateGizmo.h"
#include "../Gizmos/ScaleGizmo.h"
#include "../Gizmos/TranslateGizmo.h"
#include "../Rendering/Camera.h"
#include "../Rendering/GizmoRenderer.h"
#include "../Rendering/GridRenderer.h"
#include "../Rendering/PrimitiveMeshes.h"
#include "../Rendering/SimpleRenderer.h"
#include "EditorPanel.h"
#include "RHI/FrameGraph.h"

#include <imgui.h>

#include <array>
#include <deque>
#include <memory>
#include <unordered_map>
#include <vector>

namespace Dot
{

struct ReflectionProbeAutoGenerateResult
{
    uint32_t removedProbeCount = 0;
    uint32_t sourceBoundsCount = 0;
    uint32_t createdProbeCount = 0;
    uint32_t queuedBakeCount = 0;
};

class RHIDevice;
class RHISwapChain;
class MapDocument;
class Command;
class NavigationSystem;
class PhysicsSystem;
class EditorSceneContext;
class ViewportInteractionController;
class MapViewportToolController;

/// Gizmo mode
enum class GizmoMode
{
    Translate,
    Rotate,
    Scale
};

/// Viewport panel - 3D scene view
class ViewportPanel : public EditorPanel
{
public:
    ViewportPanel() : EditorPanel("Viewport") {}

    /// Initialize 3D rendering
    bool Initialize(RHIDevice* device);

    /// Process input (gizmos, camera) - call before RenderScene
    void ProcessInput();

    /// Render 3D scene (called before GUI)
    void RenderScene(RHISwapChain* swapChain);
    void RequestReflectionProbeBake(Entity entity);
    ReflectionProbeAutoGenerateResult RegenerateAutomaticReflectionProbes();
    uint32_t ClearAutomaticReflectionProbes();

    void OnImGui() override;

    /// Reset the world (creates a fresh empty world)
    void ResetWorld();
    /// Get the ECS world
    World& GetWorld() { return *m_World; }

    /// Get/set selected entity (for sync with hierarchy)
    Entity GetSelectedEntity() const { return m_SelectedEntity; }
    void SetSelectedEntity(Entity entity);
    const std::vector<Entity>& GetSelectedEntities() const { return m_SelectedEntities; }
    void SetSelectedEntities(const std::vector<Entity>& entities, Entity primaryEntity);
    void SetSceneContext(EditorSceneContext* sceneContext) { m_SceneContext = sceneContext; }

    /// Gizmo mode
    GizmoMode GetGizmoMode() const { return m_GizmoMode; }
    void SetGizmoMode(GizmoMode mode) { m_GizmoMode = mode; }

    /// Play mode - hides gizmos, grid, and editor overlays
    void SetPlayMode(bool playMode);
    bool IsPlayMode() const { return m_PlayMode; }

    void SetMapDocument(MapDocument* mapDocument) { m_MapDocument = mapDocument; }
    void SetSceneMapDocument(MapDocument* mapDocument) { m_SceneMapDocument = mapDocument; }
    void SetMapEditingEnabled(bool enabled) { m_MapEditingEnabled = enabled; }
    bool IsMapEditingEnabled() const { return m_MapEditingEnabled; }
    void SetMapClipToolActive(bool active) { m_MapClipToolActive = active; }
    bool IsMapClipToolActive() const { return m_MapClipToolActive; }
    void SetMapClipPreview(float offset, bool flipPlane)
    {
        m_MapClipPreviewOffset = offset;
        m_MapClipPreviewFlipPlane = flipPlane;
    }
    float GetMapClipPreviewOffset() const { return m_MapClipPreviewOffset; }
    bool GetMapClipPreviewFlipPlane() const { return m_MapClipPreviewFlipPlane; }
    bool IsMapClipDragging() const { return m_MapClipDragging; }
    void SetMapExtrudeToolActive(bool active) { m_MapExtrudeToolActive = active; }
    bool IsMapExtrudeToolActive() const { return m_MapExtrudeToolActive; }
    void SetMapExtrudePreviewDistance(float distance) { m_MapExtrudePreviewDistance = distance; }
    float GetMapExtrudePreviewDistance() const { return m_MapExtrudePreviewDistance; }
    bool IsMapExtrudeDragging() const { return m_MapExtrudeDragging; }
    void SetMapHollowPreviewActive(bool active) { m_MapHollowPreviewActive = active; }
    bool IsMapHollowPreviewActive() const { return m_MapHollowPreviewActive; }
    void SetMapHollowPreviewThickness(float thickness) { m_MapHollowPreviewThickness = thickness; }
    float GetMapHollowPreviewThickness() const { return m_MapHollowPreviewThickness; }
    void SetMapTranslationSnap(bool enabled, float step)
    {
        m_MapTranslationSnapEnabled = enabled;
        m_MapTranslationSnapStep = step;
    }
    void SetNavigationSystem(NavigationSystem* navigationSystem) { m_NavigationSystem = navigationSystem; }
    void SetPhysicsSystem(PhysicsSystem* physicsSystem) { m_PhysicsSystem = physicsSystem; }

    /// Camera access for scripting
    const Camera& GetCamera() const { return m_PlayMode ? m_PlayCamera : m_Camera; }
    const FrameGraph& GetFrameGraph() const { return m_FrameGraph; }

    /// Viewport bounds for scripting (screen coordinates)
    float GetViewportPosX() const { return m_ViewportX; }
    float GetViewportPosY() const { return m_ViewportY; }
    float GetViewportWidth() const { return m_ViewportWidth; }
    float GetViewportHeight() const { return m_ViewportHeight; }

private:
    friend class ViewportInteractionController;
    friend class MapViewportToolController;

    void HandleMouseInput();
    enum class SelectionInteraction
    {
        Replace,
        Add,
        Toggle
    };

    bool TrySelectEntity(const Ray& ray, SelectionInteraction interaction);
    void ApplyEntitySelectionInteraction(const std::vector<Entity>& entities, SelectionInteraction interaction);
    bool TrySelectMapElement(const Ray& ray, SelectionInteraction interaction);
    Gizmo* GetActiveGizmo();
    Gizmo* GetMapGizmo();
    bool HasMapSelection() const;
    void HandleMapKeyboardShortcuts();
    bool BeginMapGizmoDrag(const Ray& ray);
    bool UpdateMapGizmoDrag(const Ray& ray);
    void EndMapGizmoDrag();
    void UpdateMapGizmoPlacement();
    void UpdateEntityGizmoPlacement();
    void RebuildMapPreviewIfNeeded();
    MapDocument* GetActiveMapRenderDocument() const;
    std::vector<Entity> GetValidSelectedEntities() const;
    Vec3 ComputeSelectionPivot() const;
    void SyncSelectionFromContext();
    void PublishSelectionToContext();
    void FinalizePendingReflectionProbeBake();
    bool EnsureReflectionProbeBakeResources(uint32 resolution);
    void ExecutePendingReflectionProbeBake(RHISwapChain* swapChain);

    float m_ViewportWidth = 0;
    float m_ViewportHeight = 0;
    float m_ViewportX = 0;
    float m_ViewportY = 0;

    std::unique_ptr<SimpleRenderer> m_Renderer;
    std::unique_ptr<GizmoRenderer> m_GizmoRenderer;
    std::unique_ptr<GizmoRenderer> m_OverlayGizmoRenderer;
    std::unique_ptr<GridRenderer> m_GridRenderer;
    Camera m_Camera;     // Editor camera
    Camera m_PlayCamera; // Play mode camera (copies scene camera settings)
    bool m_Initialized = false;

    // Frame Graph for render pass management
    FrameGraph m_FrameGraph;
    RHIDevice* m_Device = nullptr;
    EditorSceneContext* m_SceneContext = nullptr;

    // ECS World (use unique_ptr to allow safe recreation without buggy Clear())
    std::unique_ptr<World> m_World;
    Entity m_SelectedEntity = kNullEntity;
    std::vector<Entity> m_SelectedEntities;

    // Gizmo system
    TranslateGizmo m_TranslateGizmo;
    RotateGizmo m_RotateGizmo;
    ScaleGizmo m_ScaleGizmo;
    GizmoMode m_GizmoMode = GizmoMode::Translate;

    // Transform space (Local = object space, World = global axes)
    enum class TransformSpace
    {
        Local,
        World
    };
    TransformSpace m_TransformSpace = TransformSpace::World;

    // Mouse state
    bool m_ViewportHovered = false;
    bool m_ViewportActive = false;
    int m_LastHandledMouseInputFrame = -1;
    bool m_MouseDragging = false; // Gizmo dragging
    bool m_IsOrbiting = false;    // Camera orbit (RMB)
    bool m_IsPanning = false;     // Camera pan (MMB)
    bool m_PlayMode = false;      // When true, hide gizmos/grid and disable editor interaction
    bool m_MapEditingEnabled = false;
    bool m_MapClipToolActive = false;
    bool m_MapClipDragging = false;
    bool m_MapClipPreviewHovered = false;
    float m_MapClipPreviewOffset = -0.25f;
    bool m_MapClipPreviewFlipPlane = false;
    float m_MapClipDragStartOffset = 0.0f;
    float m_MapClipDragStartAxisT = 0.0f;
    Vec3 m_MapClipDragAxisOrigin = Vec3::Zero();
    Vec3 m_MapClipDragAxisDirection = Vec3::Up();
    bool m_MapExtrudeToolActive = false;
    bool m_MapExtrudeDragging = false;
    bool m_MapExtrudePreviewHovered = false;
    float m_MapExtrudePreviewDistance = 0.5f;
    float m_MapExtrudeDragStartDistance = 0.0f;
    float m_MapExtrudeDragStartAxisT = 0.0f;
    Vec3 m_MapExtrudeDragAxisOrigin = Vec3::Zero();
    Vec3 m_MapExtrudeDragAxisDirection = Vec3::Up();
    bool m_MapHollowPreviewActive = false;
    float m_MapHollowPreviewThickness = 0.25f;
    bool m_EntityMarqueePending = false;
    bool m_EntityMarqueeActive = false;
    SelectionInteraction m_EntityMarqueeInteraction = SelectionInteraction::Replace;
    ImVec2 m_EntityMarqueeStart = ImVec2(0.0f, 0.0f);
    ImVec2 m_EntityMarqueeEnd = ImVec2(0.0f, 0.0f);
    bool m_MapMarqueePending = false;
    bool m_MapMarqueeActive = false;
    SelectionInteraction m_MapMarqueeInteraction = SelectionInteraction::Replace;
    ImVec2 m_MapMarqueeStart = ImVec2(0.0f, 0.0f);
    ImVec2 m_MapMarqueeEnd = ImVec2(0.0f, 0.0f);
    NavigationSystem* m_NavigationSystem = nullptr;
    PhysicsSystem* m_PhysicsSystem = nullptr;

    MapDocument* m_MapDocument = nullptr;
    MapDocument* m_SceneMapDocument = nullptr;
    const MapDocument* m_MapPreviewSourceDocument = nullptr;
    uint64_t m_MapPreviewRevision = 0;
    struct MapPreviewPart
    {
        std::unique_ptr<PrimitiveMesh> mesh;
        MaterialData material;
    };
    std::vector<MapPreviewPart> m_MapPreviewParts;

    struct HlodSourceMeshCacheEntry
    {
        bool attemptedLoad = false;
        std::vector<MeshData> meshes;
    };
    struct HlodProxyEntry
    {
        uint64_t signature = 0;
        Mat4 worldMatrix = Mat4::Identity();
        std::unique_ptr<PrimitiveMesh> mesh;
    };
    std::unordered_map<std::string, HlodSourceMeshCacheEntry> m_HlodSourceMeshCache;
    std::unordered_map<uint64_t, HlodProxyEntry> m_HlodProxyCache;

    struct CachedMaterialRuntime
    {
        LoadedMaterial loaded;
        uint32_t shaderId = 0;
    };
    std::unordered_map<std::string, std::string> m_ResolvedMaterialPathCache;
    std::unordered_map<std::string, CachedMaterialRuntime> m_MaterialRuntimeCache;

    Entity m_PendingReflectionProbeBakeEntity = kNullEntity;
    Entity m_ReflectionProbeBakeReadbackEntity = kNullEntity;
    std::deque<Entity> m_QueuedReflectionProbeBakes;
    RHITexturePtr m_ReflectionProbeBakeColorTarget;
    RHITexturePtr m_ReflectionProbeBakeDepthTarget;
    RHIBufferPtr m_ReflectionProbeBakeReadbackBuffer;
    uint32_t m_ReflectionProbeBakeResolution = 0;
    uint32_t m_ReflectionProbeBakeRowPitch = 0;
    std::array<uint64_t, 6> m_ReflectionProbeBakeFaceOffsets = {};
    std::string m_ReflectionProbeBakeRelativePath;

    // Transform state at drag start (for undo)
    Vec3 m_DragStartPosition;
    Vec3 m_DragStartRotation;
    Vec3 m_DragStartScale;
    struct TransformSelectionState
    {
        Entity entity = kNullEntity;
        Vec3 position = Vec3::Zero();
        Vec3 rotation = Vec3::Zero();
        Vec3 scale = Vec3::One();
    };
    std::vector<TransformSelectionState> m_DragStartTransforms;
    Vec3 m_DragStartPivot = Vec3::Zero();
    Vec3 m_EntityDragAccumulatedDelta = Vec3::Zero();
    Vec3 m_EntityDragAppliedDelta = Vec3::Zero();
    MapAsset m_MapDragStartAsset;
    std::vector<MapSelection> m_MapDragStartSelections;
    MapSelection m_MapDragStartSelection;
    std::unordered_set<uint32> m_MapDragStartHiddenBrushIds;
    std::unordered_set<uint32> m_MapDragStartLockedBrushIds;
    bool m_MapDragStartDirty = false;
    uint64_t m_MapDragStartRevision = 0;
    bool m_MapTranslationSnapEnabled = false;
    float m_MapTranslationSnapStep = 1.0f;
    Vec3 m_MapDragAccumulatedDelta = Vec3::Zero();
    Vec3 m_MapDragAppliedDelta = Vec3::Zero();
    float m_MapDragAccumulatedFaceDistance = 0.0f;
    float m_MapDragAppliedFaceDistance = 0.0f;

    // Saved camera state for play mode restore
    struct SavedCameraState
    {
        float posX, posY, posZ;
        float targetX, targetY, targetZ;
        float fov, aspect;
        bool valid = false;
    } m_SavedCameraState;

    // Toolbar
    void DrawToolbar();
};

} // namespace Dot
