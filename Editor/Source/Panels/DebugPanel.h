// =============================================================================
// Dot Engine - Debug Panel
// =============================================================================
// Debug suite for engine diagnostics and performance monitoring.
// =============================================================================

#pragma once

#include "Core/ECS/World.h"
#include "Core/Rendering/RenderDebugStats.h"
#include "RHI/FrameGraph.h"
#include "PanelChrome.h"

#include <imgui.h>

namespace Dot
{

class TextureViewerPanel;

/// Debug statistics collected per frame
struct DebugStats
{
    // ECS Stats
    int totalEntities = 0;
    int entitiesWithTransform = 0;
    int entitiesWithPrimitive = 0;
    int entitiesWithHierarchy = 0;
    int lightEntities = 0;

    // Render Stats
    int renderCalls = 0;
    int skippedEntities = 0;
    int hzbTests = 0;
    int hzbCulled = 0;
    int frustumTestedObjects = 0;
    int frustumChunkedObjects = 0;
    int frustumCulledChunks = 0;
    int frustumAcceptedViaChunk = 0;
    int aoDepthPrepassCandidates = 0;
    int aoDepthPrepassFrustumCulled = 0;
    int aoDepthPrepassDuplicatesSkipped = 0;
    int aoDepthPrepassDraws = 0;
    int aoPassSequenceStage = 0; // 0..4: resources, depth, ssao, blur
    // Frame timing
    float frameTimeMs = 0.0f;
    float fps = 0.0f;

    void Reset()
    {
        totalEntities = 0;
        entitiesWithTransform = 0;
        entitiesWithPrimitive = 0;
        entitiesWithHierarchy = 0;
        lightEntities = 0;
        renderCalls = 0;
        skippedEntities = 0;
        hzbTests = 0;
        hzbCulled = 0;
        frustumTestedObjects = 0;
        frustumChunkedObjects = 0;
        frustumCulledChunks = 0;
        frustumAcceptedViaChunk = 0;
        aoDepthPrepassCandidates = 0;
        aoDepthPrepassFrustumCulled = 0;
        aoDepthPrepassDuplicatesSkipped = 0;
        aoDepthPrepassDraws = 0;
        aoPassSequenceStage = 0;
    }
};

/// Global debug stats instance
inline DebugStats g_DebugStats;

/// Debug Panel - shows engine diagnostics
class DebugPanel
{
public:
    DebugPanel() = default;

    void SetWorld(World* world) { m_World = world; }
    void SetFrameGraph(const FrameGraph* frameGraph) { m_FrameGraph = frameGraph; }
    void SetTextureViewerPanel(TextureViewerPanel* textureViewerPanel) { m_TextureViewerPanel = textureViewerPanel; }

    void Draw()
    {
        if (!m_ShowPanel)
            return;

        BeginChromeWindow("Debug Suite", &m_ShowPanel);

        if (ImGui::CollapsingHeader("ECS Statistics", ImGuiTreeNodeFlags_DefaultOpen))
        {
            DrawECSStats();
        }

        if (ImGui::CollapsingHeader("Render Statistics", ImGuiTreeNodeFlags_DefaultOpen))
        {
            DrawRenderStats();
        }

        if (ImGui::CollapsingHeader("Performance", ImGuiTreeNodeFlags_DefaultOpen))
        {
            DrawPerformanceStats();
        }

        if (ImGui::CollapsingHeader("Render Graph", ImGuiTreeNodeFlags_DefaultOpen))
        {
            DrawRenderGraph();
        }

        if (ImGui::CollapsingHeader("Entity List"))
        {
            DrawEntityList();
        }

        if (ImGui::CollapsingHeader("Archetype Info"))
        {
            DrawArchetypeInfo();
        }

        if (ImGui::CollapsingHeader("Memory Statistics", ImGuiTreeNodeFlags_DefaultOpen))
        {
            DrawMemoryStats();
        }

        ImGui::End();

        if (m_ShowAdvancedRenderGraphPanel)
            DrawAdvancedRenderGraphPanel();
    }

    void Show() { m_ShowPanel = true; }
    void Hide() { m_ShowPanel = false; }
    void Toggle() { m_ShowPanel = !m_ShowPanel; }
    bool IsVisible() const { return m_ShowPanel; }

    /// Call at start of frame to collect stats
    void BeginFrame()
    {
        m_FrameStartTime = ImGui::GetTime();
        g_DebugStats.Reset();
        g_RenderDebugStats.Reset();

        if (m_World)
        {
            CollectECSStats();
        }
    }

    /// Call at end of frame
    void EndFrame()
    {
        double frameTime = ImGui::GetTime() - m_FrameStartTime;
        g_DebugStats.frameTimeMs = static_cast<float>(frameTime * 1000.0);
        g_DebugStats.fps = frameTime > 0 ? static_cast<float>(1.0 / frameTime) : 0.0f;
    }

private:
    enum class RenderGraphSelectionType : uint8
    {
        None = 0,
        Pass,
        Resource,
    };

    void CollectECSStats();
    void DrawECSStats();
    void DrawRenderStats();
    void DrawRenderGraph();
    void DrawAdvancedRenderGraphPanel();
    void DrawRenderGraphCanvas(bool allowPanZoom, float height);
    void DrawRenderGraphDetails(float height);
    void DrawPerformanceStats();
    void DrawEntityList();
    void DrawArchetypeInfo();
    void DrawMemoryStats();

    World* m_World = nullptr;
    const FrameGraph* m_FrameGraph = nullptr;
    TextureViewerPanel* m_TextureViewerPanel = nullptr;
    bool m_ShowPanel = false;
    double m_FrameStartTime = 0.0;
    bool m_ShowAdvancedRenderGraphPanel = false;
    bool m_RenderGraphHeatmapEnabled = false;
    bool m_RenderGraphStageColoringEnabled = true;
    bool m_RenderGraphAutoFitPending = false;
    RenderGraphSelectionType m_RenderGraphSelectionType = RenderGraphSelectionType::None;
    uint32 m_SelectedRenderGraphPass = UINT32_MAX;
    FrameGraphResourceHandle m_SelectedRenderGraphResource;
    float m_RenderGraphCanvasZoom = 1.0f;
    ImVec2 m_RenderGraphCanvasPan = ImVec2(40.0f, 30.0f);
};

} // namespace Dot
