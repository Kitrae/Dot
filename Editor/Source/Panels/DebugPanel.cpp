// =============================================================================
// Dot Engine - Debug Panel Implementation
// =============================================================================

#include "DebugPanel.h"

#include "Core/Memory/MemorySystem.h"
#include "Core/Scene/Components.h"
#include "Core/Scene/LightComponent.h"
#include "TextureViewerPanel.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <string>
#include <unordered_set>
#include <vector>

#include <imgui.h>
#include <imgui_internal.h>

namespace Dot
{

namespace
{

struct RenderGraphPassNode
{
    uint32 passIndex = UINT32_MAX;
    ImRect rect;
};

struct RenderGraphResourceNode
{
    FrameGraphResourceHandle handle;
    ImRect rect;
};

struct RenderGraphLogicalResourceRow
{
    uint32 logicalId = UINT32_MAX;
    std::vector<const FrameGraphResource*> versions;
};

const char* ToString(FrameGraphResourceAccess access)
{
    switch (access)
    {
        case FrameGraphResourceAccess::Read:
            return "Read";
        case FrameGraphResourceAccess::Write:
            return "Write";
        case FrameGraphResourceAccess::ReadWrite:
            return "ReadWrite";
        case FrameGraphResourceAccess::None:
        default:
            return "None";
    }
}

const char* ToString(FrameGraphResourceUsage usage)
{
    switch (usage)
    {
        case FrameGraphResourceUsage::ShaderRead:
            return "ShaderRead";
        case FrameGraphResourceUsage::ColorAttachment:
            return "ColorAttachment";
        case FrameGraphResourceUsage::DepthStencilRead:
            return "DepthRead";
        case FrameGraphResourceUsage::DepthStencilWrite:
            return "DepthWrite";
        case FrameGraphResourceUsage::Storage:
            return "Storage";
        case FrameGraphResourceUsage::CopySource:
            return "CopySource";
        case FrameGraphResourceUsage::CopyDest:
            return "CopyDest";
        case FrameGraphResourceUsage::Present:
            return "Present";
        case FrameGraphResourceUsage::Unknown:
        default:
            return "Unknown";
    }
}

const char* ToString(RHIResourceState state)
{
    switch (state)
    {
        case RHIResourceState::Common:
            return "Common";
        case RHIResourceState::RenderTarget:
            return "RenderTarget";
        case RHIResourceState::DepthWrite:
            return "DepthWrite";
        case RHIResourceState::DepthRead:
            return "DepthRead";
        case RHIResourceState::ShaderResource:
            return "ShaderRead";
        case RHIResourceState::UnorderedAccess:
            return "UAV";
        case RHIResourceState::CopySource:
            return "CopySource";
        case RHIResourceState::CopyDest:
            return "CopyDest";
        case RHIResourceState::Present:
            return "Present";
        case RHIResourceState::Unknown:
        default:
            return "Unknown";
    }
}

const char* ToString(FrameGraphResourceType type)
{
    return type == FrameGraphResourceType::Texture ? "Texture" : "Buffer";
}

bool ContainsHandle(const std::vector<FrameGraphResourceHandle>& handles, FrameGraphResourceHandle handle)
{
    return std::find(handles.begin(), handles.end(), handle) != handles.end();
}

ImU32 ColorU32(float r, float g, float b, float a = 1.0f)
{
    return ImGui::ColorConvertFloat4ToU32(ImVec4(r, g, b, a));
}

ImVec4 PassFillColor(const FrameGraphPass& pass)
{
    if (pass.culled)
        return ImVec4(0.22f, 0.24f, 0.27f, 1.0f);
    if (pass.hasSideEffects)
        return ImVec4(0.39f, 0.29f, 0.14f, 1.0f);
    return ImVec4(0.14f, 0.22f, 0.36f, 1.0f);
}

ImVec4 ResourceFillColor(const FrameGraphResource& resource, bool isOutput)
{
    if (isOutput)
        return ImVec4(0.34f, 0.28f, 0.08f, 1.0f);
    if (resource.imported)
        return ImVec4(0.14f, 0.32f, 0.20f, 1.0f);
    if (resource.desc.type == FrameGraphResourceType::Buffer)
        return ImVec4(0.25f, 0.16f, 0.31f, 1.0f);
    return ImVec4(0.19f, 0.17f, 0.36f, 1.0f);
}

std::string ToLower(std::string value)
{
    for (char& c : value)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return value;
}

ImVec4 StageColorForPass(const FrameGraphPass& pass)
{
    const std::string name = ToLower(pass.name);
    if (name.find("shadow") != std::string::npos)
        return ImVec4(0.45f, 0.30f, 0.16f, 1.0f);
    if (name.find("depth") != std::string::npos || name.find("hzb") != std::string::npos)
        return ImVec4(0.16f, 0.34f, 0.44f, 1.0f);
    if (name.find("ssao") != std::string::npos || name.find("blur") != std::string::npos ||
        name.find("composite") != std::string::npos || name.find("fxaa") != std::string::npos)
    {
        return ImVec4(0.32f, 0.18f, 0.48f, 1.0f);
    }
    if (name.find("viewmodel") != std::string::npos)
        return ImVec4(0.18f, 0.44f, 0.32f, 1.0f);
    if (name.find("gizmo") != std::string::npos || name.find("light") != std::string::npos ||
        name.find("overlay") != std::string::npos)
    {
        return ImVec4(0.28f, 0.36f, 0.18f, 1.0f);
    }
    if (name.find("present") != std::string::npos)
        return ImVec4(0.30f, 0.30f, 0.30f, 1.0f);
    return ImVec4(0.14f, 0.22f, 0.36f, 1.0f);
}

ImVec4 HeatColorForPass(const FrameGraphPass& pass, double maxGpuTimeMs)
{
    if (pass.culled || maxGpuTimeMs <= 0.0 || pass.gpuTimeMs <= 0.0)
        return PassFillColor(pass);

    const float t = static_cast<float>((std::min)(1.0, pass.gpuTimeMs / maxGpuTimeMs));
    const ImVec4 cool(0.17f, 0.35f, 0.82f, 1.0f);
    const ImVec4 hot(0.88f, 0.23f, 0.16f, 1.0f);
    return ImVec4(cool.x + (hot.x - cool.x) * t, cool.y + (hot.y - cool.y) * t, cool.z + (hot.z - cool.z) * t, 1.0f);
}

usize EstimateFormatBytesPerPixel(RHIFormat format)
{
    switch (format)
    {
        case RHIFormat::R8_UNORM:
            return 1;
        case RHIFormat::R8G8_UNORM:
        case RHIFormat::R16_FLOAT:
        case RHIFormat::R16_UINT:
        case RHIFormat::D16_UNORM:
            return 2;
        case RHIFormat::R8G8B8A8_UNORM:
        case RHIFormat::R8G8B8A8_SRGB:
        case RHIFormat::B8G8R8A8_UNORM:
        case RHIFormat::B8G8R8A8_SRGB:
        case RHIFormat::R16G16_FLOAT:
        case RHIFormat::R32_FLOAT:
        case RHIFormat::R32_UINT:
        case RHIFormat::D24_UNORM_S8_UINT:
        case RHIFormat::D32_FLOAT:
            return 4;
        case RHIFormat::R16G16B16A16_FLOAT:
        case RHIFormat::R32G32_FLOAT:
        case RHIFormat::D32_FLOAT_S8_UINT:
            return 8;
        case RHIFormat::R32G32B32_FLOAT:
            return 12;
        case RHIFormat::R32G32B32A32_FLOAT:
            return 16;
        case RHIFormat::Unknown:
        case RHIFormat::Count:
        default:
            return 4;
    }
}

usize EstimateResourceBytes(const FrameGraphResourceDesc& desc)
{
    if (desc.type == FrameGraphResourceType::Buffer)
        return desc.bufferSize;

    const usize pixelBytes = EstimateFormatBytesPerPixel(desc.format);
    const usize width = (std::max)(1u, desc.width);
    const usize height = (std::max)(1u, desc.height);
    const usize depth = (std::max)(1u, desc.depth);
    const usize layers = (std::max)(1u, desc.arrayLayers);
    const usize samples = (std::max)(1u, desc.sampleCount);
    const usize mips = (std::max)(1u, desc.mipLevels);

    usize total = 0;
    usize mipWidth = width;
    usize mipHeight = height;
    usize mipDepth = depth;
    for (usize mip = 0; mip < mips; ++mip)
    {
        total += mipWidth * mipHeight * mipDepth * layers * samples * pixelBytes;
        mipWidth = (std::max)(1ull, mipWidth >> 1u);
        mipHeight = (std::max)(1ull, mipHeight >> 1u);
        mipDepth = (std::max)(1ull, mipDepth >> 1u);
    }
    return total;
}

std::string FormatBytes(usize bytes)
{
    char buffer[64];
    const double value = static_cast<double>(bytes);
    if (value >= 1024.0 * 1024.0)
    {
        std::snprintf(buffer, sizeof(buffer), "%.2f MB", value / (1024.0 * 1024.0));
        return buffer;
    }
    if (value >= 1024.0)
    {
        std::snprintf(buffer, sizeof(buffer), "%.1f KB", value / 1024.0);
        return buffer;
    }
    std::snprintf(buffer, sizeof(buffer), "%llu B", static_cast<unsigned long long>(bytes));
    return buffer;
}

void DrawMetricCard(const char* label, const char* value, const ImVec2& size, const ImVec4& accent)
{
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.08f, 0.09f, 0.11f, 1.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 8.0f);
    ImGui::BeginChild(label, size, true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const ImVec2 min = ImGui::GetWindowPos();
    const ImVec2 max = ImVec2(min.x + ImGui::GetWindowSize().x, min.y + ImGui::GetWindowSize().y);
    drawList->AddRectFilled(min, ImVec2(min.x + 4.0f, max.y), ImGui::ColorConvertFloat4ToU32(accent), 8.0f,
                            ImDrawFlags_RoundCornersLeft);
    ImGui::TextDisabled("%s", label);
    ImGui::Spacing();
    ImGui::Text("%s", value);
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
}

void DrawSectionHeader(const char* label, const char* caption = nullptr)
{
    ImGui::Text("%s", label);
    if (caption)
    {
        ImGui::SameLine();
        ImGui::TextDisabled("%s", caption);
    }
}

std::string MakeDisplayName(const std::string& raw, bool stripPassSuffix)
{
    std::string value = raw;
    if (stripPassSuffix && value.size() > 4 && value.substr(value.size() - 4) == "Pass")
        value.resize(value.size() - 4);

    std::string result;
    result.reserve(value.size() + 8);
    for (size_t i = 0; i < value.size(); ++i)
    {
        const char c = value[i];
        const bool isUpper = (c >= 'A' && c <= 'Z');
        const bool prevLower = i > 0 && value[i - 1] >= 'a' && value[i - 1] <= 'z';
        if (i > 0 && (c == '_' || c == '-'))
        {
            result.push_back(' ');
            continue;
        }
        if (i > 0 && isUpper && prevLower)
            result.push_back(' ');
        result.push_back(c);
    }
    return result;
}

std::string TruncateLabel(const std::string& text, size_t maxChars)
{
    if (text.size() <= maxChars)
        return text;
    if (maxChars <= 3)
        return text.substr(0, maxChars);
    return text.substr(0, maxChars - 3) + "...";
}

bool CanPreviewFrameGraphTexture(const FrameGraphResource* resource)
{
    return resource && resource->desc.type == FrameGraphResourceType::Texture && resource->texture != nullptr;
}

} // namespace

void DebugPanel::CollectECSStats()
{
    if (!m_World)
        return;

    // Count all entities
    m_World->EachEntity(
        [this](Entity entity)
        {
            g_DebugStats.totalEntities++;

            if (m_World->HasComponent<TransformComponent>(entity))
                g_DebugStats.entitiesWithTransform++;

            if (m_World->HasComponent<PrimitiveComponent>(entity))
                g_DebugStats.entitiesWithPrimitive++;

            if (m_World->HasComponent<HierarchyComponent>(entity))
                g_DebugStats.entitiesWithHierarchy++;

            if (m_World->HasComponent<DirectionalLightComponent>(entity) ||
                m_World->HasComponent<PointLightComponent>(entity) || m_World->HasComponent<SpotLightComponent>(entity))
            {
                g_DebugStats.lightEntities++;
            }
        });
}

void DebugPanel::DrawECSStats()
{
    ImGui::Text("Total Entities: %d", g_DebugStats.totalEntities);
    ImGui::Text("With Transform: %d", g_DebugStats.entitiesWithTransform);
    ImGui::Text("With Primitive: %d", g_DebugStats.entitiesWithPrimitive);
    ImGui::Text("With Hierarchy: %d", g_DebugStats.entitiesWithHierarchy);
    ImGui::Text("Light Entities: %d", g_DebugStats.lightEntities);

    ImGui::Separator();

    // Show expected vs actual
    int expectedRenders = g_DebugStats.entitiesWithTransform - g_DebugStats.lightEntities;
    ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "Expected Renders: %d (Transform - Lights)", expectedRenders);
}

void DebugPanel::DrawRenderStats()
{
    ImGui::Text("Render Calls: %d", g_DebugStats.renderCalls);
    ImGui::Text("Skipped (Cull/Lights): %d", g_DebugStats.skippedEntities);
    ImGui::Text("HZB Tests: %d", g_DebugStats.hzbTests);
    ImGui::Text("HZB Culled: %d", g_DebugStats.hzbCulled);
    ImGui::Text("Frustum Tested Objects: %d", g_DebugStats.frustumTestedObjects);
    ImGui::Text("Frustum Chunked Objects: %d", g_DebugStats.frustumChunkedObjects);
    ImGui::Text("Frustum Culled Chunks: %d", g_DebugStats.frustumCulledChunks);
    ImGui::Text("Frustum Accepted Via Chunk: %d", g_DebugStats.frustumAcceptedViaChunk);
    ImGui::Text("LOD Draws: L0 %d | L1 %d | L2 %d", g_RenderDebugStats.lod0Draws, g_RenderDebugStats.lod1Draws,
                g_RenderDebugStats.lod2Draws);
    ImGui::Separator();
    ImGui::Text("AO Prepass Candidates: %d", g_DebugStats.aoDepthPrepassCandidates);
    ImGui::Text("AO Prepass Frustum Culled: %d", g_DebugStats.aoDepthPrepassFrustumCulled);
    ImGui::Text("AO Prepass Duplicates Skipped: %d", g_DebugStats.aoDepthPrepassDuplicatesSkipped);
    ImGui::Text("AO Prepass Draws: %d", g_DebugStats.aoDepthPrepassDraws);
    ImGui::Text("AO Pass Sequence Stage: %d/4", g_DebugStats.aoPassSequenceStage);

    // Highlight mismatch
    int expectedRenders = g_DebugStats.entitiesWithTransform - g_DebugStats.lightEntities;
    if (g_DebugStats.renderCalls != expectedRenders)
    {
        ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "WARNING: Render count mismatch!");
        ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "Expected: %d, Actual: %d", expectedRenders,
                           g_DebugStats.renderCalls);
    }
}

void DebugPanel::DrawPerformanceStats()
{
    ImGui::Text("Frame Time: %.2f ms", g_DebugStats.frameTimeMs);
    ImGui::Text("FPS: %.1f", g_DebugStats.fps);

    // FPS history graph
    static float fpsHistory[60] = {0};
    static int historyIndex = 0;
    fpsHistory[historyIndex] = g_DebugStats.fps;
    historyIndex = (historyIndex + 1) % 60;

    ImGui::PlotLines("FPS", fpsHistory, 60, historyIndex, nullptr, 0.0f, 120.0f, ImVec2(0, 50));
}

void DebugPanel::DrawRenderGraph()
{
    if (!m_FrameGraph)
    {
        ImGui::TextDisabled("No render graph attached");
        return;
    }

    char compiledText[32];
    char passText[64];
    char resourceText[64];
    char timingText[64];
    std::snprintf(compiledText, sizeof(compiledText), "%s", m_FrameGraph->IsCompiled() ? "Compiled" : "Dirty");
    std::snprintf(passText, sizeof(passText), "%zu total / %zu active", m_FrameGraph->GetPassCount(),
                  m_FrameGraph->GetActivePassCount());
    std::snprintf(resourceText, sizeof(resourceText), "%zu logical / %zu physical", m_FrameGraph->GetResourceCount(),
                  m_FrameGraph->GetPhysicalResourceCount());
    std::snprintf(timingText, sizeof(timingText), "%.3f / %.3f ms", m_FrameGraph->GetLastCompileTimeMs(),
                  m_FrameGraph->GetLastExecuteTimeMs());

    const float availableWidth = ImGui::GetContentRegionAvail().x;
    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    const float cardWidth = (availableWidth - spacing * 3.0f) * 0.25f;

    DrawMetricCard("State", compiledText, ImVec2(cardWidth, 64.0f), ImVec4(0.22f, 0.62f, 0.92f, 1.0f));
    ImGui::SameLine();
    DrawMetricCard("Passes", passText, ImVec2(cardWidth, 64.0f), ImVec4(0.40f, 0.72f, 0.42f, 1.0f));
    ImGui::SameLine();
    DrawMetricCard("Resources", resourceText, ImVec2(cardWidth, 64.0f), ImVec4(0.78f, 0.52f, 0.22f, 1.0f));
    ImGui::SameLine();
    DrawMetricCard("CPU (Compile / Execute)", timingText, ImVec2(cardWidth, 64.0f), ImVec4(0.70f, 0.42f, 0.88f, 1.0f));

    if (m_FrameGraph->HasValidationErrors())
    {
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.18f, 0.09f, 0.08f, 1.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 8.0f);
        ImGui::BeginChild("RenderGraphValidation", ImVec2(0.0f, 78.0f), true,
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        ImGui::TextColored(ImVec4(1.0f, 0.62f, 0.32f, 1.0f), "Validation");
        ImGui::Text("%zu issue(s) detected in the current compiled graph.", m_FrameGraph->GetValidationErrors().size());
        for (const std::string& error : m_FrameGraph->GetValidationErrors())
            ImGui::BulletText("%s", error.c_str());
        ImGui::EndChild();
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();
    }

    ImGui::Spacing();
    if (ImGui::Button("Open Advanced Mode"))
    {
        m_ShowAdvancedRenderGraphPanel = true;
        m_RenderGraphAutoFitPending = true;
    }
    ImGui::SameLine();
    ImGui::TextDisabled("Visual graph only appears in Advanced Mode.");

    if (ImGui::TreeNode("Pass Summary"))
    {
        for (const FrameGraphPass& pass : m_FrameGraph->GetPasses())
        {
            char summary[96];
            if (pass.culled)
                std::snprintf(summary, sizeof(summary), "culled");
            else if (pass.gpuTimeMs > 0.0)
                std::snprintf(summary, sizeof(summary), "exec %u  cpu %.3f  gpu %.3f", pass.executionIndex, pass.cpuTimeMs,
                              pass.gpuTimeMs);
            else
                std::snprintf(summary, sizeof(summary), "exec %u  cpu %.3f", pass.executionIndex, pass.cpuTimeMs);

            const std::string displayName = MakeDisplayName(pass.name, true);
            ImGui::BulletText("%s", displayName.c_str());
            ImGui::SameLine();
            ImGui::TextDisabled("%s", summary);
        }
        ImGui::TreePop();
    }

    if (ImGui::TreeNode("Resource Summary"))
    {
        for (const FrameGraphResource& resource : m_FrameGraph->GetResources())
        {
            ImGui::BulletText("%s v%u", resource.desc.name.c_str(), resource.version);
            ImGui::SameLine();
            ImGui::TextDisabled("L%u  phys %d  %s", resource.logicalId, resource.physicalIndex,
                                resource.imported ? "imported" : "transient");
        }
        ImGui::TreePop();
    }
}

void DebugPanel::DrawAdvancedRenderGraphPanel()
{
    if (!m_FrameGraph)
        return;

    if (!BeginChromeWindow("Render Graph", &m_ShowAdvancedRenderGraphPanel))
    {
        ImGui::End();
        return;
    }

    ImGui::Text("Advanced Mode");
    ImGui::SameLine();
    ImGui::TextDisabled("dockable graph canvas with zoom, pan, and full inspector");
    ImGui::Separator();

    if (ImGui::Button("Reset View"))
    {
        m_RenderGraphCanvasZoom = 1.0f;
        m_RenderGraphCanvasPan = ImVec2(40.0f, 30.0f);
    }
    ImGui::SameLine();
    if (ImGui::Button("Center"))
        m_RenderGraphCanvasPan = ImVec2(220.0f, 40.0f);
    ImGui::SameLine();
    if (ImGui::Button("Fit Graph"))
        m_RenderGraphAutoFitPending = true;
    ImGui::SameLine();
    ImGui::SetNextItemWidth(180.0f);
    if (ImGui::SliderFloat("Zoom", &m_RenderGraphCanvasZoom, 0.35f, 2.5f, "%.2fx"))
        m_RenderGraphCanvasZoom = (std::clamp)(m_RenderGraphCanvasZoom, 0.35f, 2.5f);
    ImGui::Checkbox("Stage Colors", &m_RenderGraphStageColoringEnabled);
    ImGui::SameLine();
    ImGui::Checkbox("GPU Heatmap", &m_RenderGraphHeatmapEnabled);
    ImGui::SameLine();
    ImGui::TextDisabled("MMB/RMB drag pans. Mouse wheel zooms.");

    const float height = (std::max)(420.0f, ImGui::GetContentRegionAvail().y - 4.0f);
    if (ImGui::BeginTable("AdvancedRenderGraphLayout", 2,
                          ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV))
    {
        ImGui::TableSetupColumn("Graph", ImGuiTableColumnFlags_WidthStretch, 0.74f);
        ImGui::TableSetupColumn("Details", ImGuiTableColumnFlags_WidthStretch, 0.26f);
        ImGui::TableNextRow();

        ImGui::TableSetColumnIndex(0);
        DrawRenderGraphCanvas(true, height);

        ImGui::TableSetColumnIndex(1);
        DrawRenderGraphDetails(height);

        ImGui::EndTable();
    }

    ImGui::End();
}

void DebugPanel::DrawRenderGraphCanvas(bool allowPanZoom, float height)
{
    DrawSectionHeader("Frame Graph", "select a pass or resource to inspect");

    const auto& passes = m_FrameGraph->GetPasses();
    const auto& resources = m_FrameGraph->GetResources();
    const auto& outputs = m_FrameGraph->GetOutputs();

    std::vector<RenderGraphLogicalResourceRow> rows;
    rows.reserve(resources.size());
    for (const FrameGraphResource& resource : resources)
    {
        auto it = std::find_if(rows.begin(), rows.end(),
                               [&](const RenderGraphLogicalResourceRow& row) { return row.logicalId == resource.logicalId; });
        if (it == rows.end())
        {
            rows.push_back({});
            rows.back().logicalId = resource.logicalId;
            it = rows.end() - 1;
        }
        it->versions.push_back(&resource);
    }
    std::sort(rows.begin(), rows.end(),
              [](const RenderGraphLogicalResourceRow& a, const RenderGraphLogicalResourceRow& b)
              {
                  const uint32 aFirst = a.versions.empty() ? UINT32_MAX : a.versions.front()->firstPass;
                  const uint32 bFirst = b.versions.empty() ? UINT32_MAX : b.versions.front()->firstPass;
                  if (aFirst != bFirst)
                      return aFirst < bFirst;
                  const std::string aName = a.versions.empty() ? std::string() : a.versions.front()->desc.name;
                  const std::string bName = b.versions.empty() ? std::string() : b.versions.front()->desc.name;
                  return aName < bName;
              });
    for (RenderGraphLogicalResourceRow& row : rows)
    {
        std::sort(row.versions.begin(), row.versions.end(),
                  [](const FrameGraphResource* a, const FrameGraphResource* b) { return a->version < b->version; });
    }

    double maxGpuTimeMs = 0.0;
    for (const FrameGraphPass& pass : passes)
        maxGpuTimeMs = (std::max)(maxGpuTimeMs, pass.gpuTimeMs);

    const float basePassWidth = 168.0f;
    const float basePassHeight = 78.0f;
    const float basePassSpacing = 62.0f;
    const float leftGutterWidth = 170.0f;
    const float basePassStartX = leftGutterWidth + 28.0f;
    const float basePassY = 54.0f;
    const float baseResourceWidth = 124.0f;
    const float baseResourceHeight = 54.0f;
    const float baseRowStartY = 192.0f;
    const float baseRowSpacing = 74.0f;
    const float zoom = allowPanZoom ? m_RenderGraphCanvasZoom : 1.0f;
    const ImVec2 pan = allowPanZoom ? m_RenderGraphCanvasPan : ImVec2(40.0f, 30.0f);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.05f, 0.06f, 0.08f, 1.0f));
    ImGui::BeginChild(allowPanZoom ? "RenderGraphCanvasAdvanced" : "RenderGraphCanvas", ImVec2(0.0f, height), true,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    const ImVec2 viewportMin = ImGui::GetCursorScreenPos();
    const ImVec2 viewportSize = ImGui::GetContentRegionAvail();
    const float graphWidth = basePassStartX + passes.size() * (basePassWidth + basePassSpacing) + 180.0f;
    const float graphHeight = baseRowStartY + rows.size() * baseRowSpacing + 80.0f;

    if (allowPanZoom && m_RenderGraphAutoFitPending && viewportSize.x > 10.0f && viewportSize.y > 10.0f)
    {
        const float fitZoomX = (viewportSize.x - 32.0f) / graphWidth;
        const float fitZoomY = (viewportSize.y - 32.0f) / graphHeight;
        m_RenderGraphCanvasZoom = (std::clamp)((std::min)(fitZoomX, fitZoomY), 0.35f, 1.6f);
        m_RenderGraphCanvasPan = ImVec2(16.0f, 16.0f);
        m_RenderGraphAutoFitPending = false;
    }

    ImGui::InvisibleButton("RenderGraphCanvasArea", viewportSize);
    const ImRect canvasRect(viewportMin, ImVec2(viewportMin.x + viewportSize.x, viewportMin.y + viewportSize.y));
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const ImU32 gridColor = ColorU32(0.12f, 0.14f, 0.18f, 1.0f);
    const ImVec2 canvasOrigin(canvasRect.Min.x + pan.x, canvasRect.Min.y + pan.y);
    auto worldToScreen = [&](const ImVec2& p) { return ImVec2(canvasOrigin.x + p.x * zoom, canvasOrigin.y + p.y * zoom); };
    auto scaleSize = [&](const ImVec2& p) { return ImVec2(p.x * zoom, p.y * zoom); };

    if (allowPanZoom && ImGui::IsItemHovered())
    {
        ImGuiIO& io = ImGui::GetIO();
        if (ImGui::IsMouseDragging(ImGuiMouseButton_Middle, 0.0f) || ImGui::IsMouseDragging(ImGuiMouseButton_Right, 0.0f))
        {
            m_RenderGraphCanvasPan.x += io.MouseDelta.x;
            m_RenderGraphCanvasPan.y += io.MouseDelta.y;
        }
        if (io.MouseWheel != 0.0f)
        {
            m_RenderGraphCanvasZoom = (std::clamp)(m_RenderGraphCanvasZoom * (1.0f + io.MouseWheel * 0.12f), 0.35f, 2.5f);
        }
    }

    const float gridStep = 40.0f * zoom;
    const float startX = canvasRect.Min.x + std::fmod(pan.x, gridStep);
    const float startY = canvasRect.Min.y + std::fmod(pan.y, gridStep);
    for (float x = startX; x < canvasRect.Max.x; x += gridStep)
        drawList->AddLine(ImVec2(x, canvasRect.Min.y), ImVec2(x, canvasRect.Max.y), gridColor);
    for (float y = startY; y < canvasRect.Max.y; y += gridStep)
        drawList->AddLine(ImVec2(canvasRect.Min.x, y), ImVec2(canvasRect.Max.x, y), gridColor);

    drawList->AddText(ImVec2(canvasRect.Min.x + 12.0f, canvasRect.Min.y + 12.0f), ColorU32(0.72f, 0.76f, 0.82f, 1.0f),
                      "Pass Timeline");
    drawList->AddText(ImVec2(canvasRect.Min.x + 12.0f, canvasRect.Min.y + baseRowStartY * zoom - 28.0f),
                      ColorU32(0.72f, 0.76f, 0.82f, 1.0f), "Resource Versions");
    const float gutterX = canvasRect.Min.x + leftGutterWidth * zoom;
    drawList->AddRectFilled(canvasRect.Min, ImVec2(gutterX, canvasRect.Max.y), ColorU32(0.07f, 0.08f, 0.10f, 0.96f));
    drawList->AddLine(ImVec2(gutterX, canvasRect.Min.y), ImVec2(gutterX, canvasRect.Max.y), ColorU32(0.24f, 0.28f, 0.34f, 1.0f),
                      1.5f);
    drawList->PushClipRect(canvasRect.Min, canvasRect.Max, true);

    std::vector<RenderGraphPassNode> passNodes;
    std::vector<RenderGraphResourceNode> resourceNodes;
    passNodes.reserve(passes.size());
    resourceNodes.reserve(resources.size());

    for (const FrameGraphPass& pass : passes)
    {
        const float x = basePassStartX + pass.index * (basePassWidth + basePassSpacing);
        const ImVec2 min = worldToScreen(ImVec2(x, basePassY));
        const ImVec2 passSize = scaleSize(ImVec2(basePassWidth, basePassHeight));
        const ImVec2 max(min.x + passSize.x, min.y + passSize.y);
        const ImRect rect(min, max);
        passNodes.push_back({pass.index, rect});
    }

    for (size_t rowIndex = 0; rowIndex < rows.size(); ++rowIndex)
    {
        const float y = baseRowStartY + rowIndex * baseRowSpacing;
        const float rowY = worldToScreen(ImVec2(0.0f, y)).y;
        const ImU32 rowColor = ColorU32(0.10f, 0.11f, 0.14f, (rowIndex % 2) == 0 ? 1.0f : 0.55f);
        drawList->AddRectFilled(ImVec2(gutterX + 8.0f, rowY - 14.0f * zoom),
                                ImVec2(canvasRect.Max.x - 12.0f, rowY + 54.0f * zoom),
                                rowColor, 6.0f);

        const FrameGraphResource* laneResource = rows[rowIndex].versions.empty() ? nullptr : rows[rowIndex].versions.front();
        const std::string laneName = laneResource ? TruncateLabel(MakeDisplayName(laneResource->desc.name, false), 18) : "Resource";
        const std::string laneMeta =
            laneResource ? std::string(laneResource->imported ? "imported" : "transient") : std::string();
        drawList->AddText(ImVec2(canvasRect.Min.x + 14.0f, rowY + 2.0f * zoom), ColorU32(0.90f, 0.92f, 0.96f, 1.0f),
                          laneName.c_str());
        if (!laneMeta.empty())
        {
            drawList->AddText(ImVec2(canvasRect.Min.x + 14.0f, rowY + 20.0f * zoom), ColorU32(0.54f, 0.60f, 0.68f, 1.0f),
                              laneMeta.c_str());
        }

        for (const FrameGraphResource* resource : rows[rowIndex].versions)
        {
            float x = leftGutterWidth + 22.0f;
            if (resource->producerPass != UINT32_MAX && resource->producerPass < passNodes.size())
            {
                const ImRect& passRect = passNodes[resource->producerPass].rect;
                x = (passRect.Min.x - canvasOrigin.x) / zoom + 2.0f;
            }
            x += resource->version * 18.0f;
            const ImVec2 min = worldToScreen(ImVec2(x, y));
            const ImVec2 resourceSize = scaleSize(ImVec2(baseResourceWidth, baseResourceHeight));
            const ImVec2 max(min.x + resourceSize.x, min.y + resourceSize.y);
            const ImRect rect(min, max);
            resourceNodes.push_back({resource->handle, rect});
        }
    }

    auto findPassRect = [&](uint32 passIndex) -> const ImRect*
    {
        for (const RenderGraphPassNode& node : passNodes)
        {
            if (node.passIndex == passIndex)
                return &node.rect;
        }
        return nullptr;
    };

    auto findResourceRect = [&](FrameGraphResourceHandle handle) -> const ImRect*
    {
        for (const RenderGraphResourceNode& node : resourceNodes)
        {
            if (node.handle == handle)
                return &node.rect;
        }
        return nullptr;
    };

    for (const FrameGraphPass& pass : passes)
    {
        const ImRect* passRect = findPassRect(pass.index);
        if (!passRect)
            continue;

        for (uint32 dependency : pass.dependencies)
        {
            const ImRect* depRect = findPassRect(dependency);
            if (!depRect)
                continue;
            const ImVec2 start(depRect->Max.x, depRect->Min.y + depRect->GetHeight() * 0.35f);
            const ImVec2 end(passRect->Min.x, passRect->Min.y + passRect->GetHeight() * 0.35f);
            drawList->AddBezierCubic(start, ImVec2(start.x + 40.0f, start.y), ImVec2(end.x - 40.0f, end.y), end,
                                     ColorU32(0.36f, 0.39f, 0.46f, 0.9f), 2.0f);
        }

        for (const FrameGraphResourceUse& use : pass.resourceUses)
        {
            const ImRect* resourceRect = findResourceRect(use.handle);
            if (!resourceRect)
                continue;
            const bool readOnly = use.access == FrameGraphResourceAccess::Read;
            const ImVec2 start = readOnly ? ImVec2(resourceRect->Max.x, resourceRect->GetCenter().y)
                                          : ImVec2(passRect->GetCenter().x, passRect->Max.y);
            const ImVec2 end = readOnly ? ImVec2(passRect->GetCenter().x, passRect->Max.y)
                                        : ImVec2(resourceRect->Min.x, resourceRect->GetCenter().y);
            const ImU32 color = readOnly ? ColorU32(0.36f, 0.67f, 0.98f, 0.88f)
                                         : ColorU32(0.96f, 0.69f, 0.32f, 0.92f);
            drawList->AddBezierCubic(start, ImVec2(start.x + (readOnly ? 30.0f : 0.0f), start.y),
                                     ImVec2(end.x - (readOnly ? 0.0f : 30.0f), end.y), end, color, 2.0f);
        }
    }

    for (const RenderGraphPassNode& node : passNodes)
    {
        const FrameGraphPass& pass = passes[node.passIndex];
        const bool selected = m_RenderGraphSelectionType == RenderGraphSelectionType::Pass &&
                              m_SelectedRenderGraphPass == pass.index;
        ImVec4 fill = PassFillColor(pass);
        if (m_RenderGraphHeatmapEnabled)
            fill = HeatColorForPass(pass, maxGpuTimeMs);
        else if (m_RenderGraphStageColoringEnabled)
            fill = StageColorForPass(pass);
        drawList->AddRectFilled(node.rect.Min, node.rect.Max, ImGui::ColorConvertFloat4ToU32(fill), 10.0f);
        drawList->AddRect(node.rect.Min, node.rect.Max,
                          selected ? ColorU32(0.94f, 0.86f, 0.40f, 1.0f) : ColorU32(0.34f, 0.40f, 0.50f, 1.0f), 10.0f,
                          0, selected ? 3.0f : 1.5f);
        const std::string passName = TruncateLabel(MakeDisplayName(pass.name, true), 16);
        drawList->AddText(ImVec2(node.rect.Min.x + 12.0f, node.rect.Min.y + 10.0f), ColorU32(0.92f, 0.95f, 0.98f, 1.0f),
                          passName.c_str());

        char lineA[48];
        if (pass.culled)
            std::snprintf(lineA, sizeof(lineA), "culled");
        else if (pass.gpuTimeMs > 0.0)
            std::snprintf(lineA, sizeof(lineA), "%.2f / %.2f ms", pass.cpuTimeMs, pass.gpuTimeMs);
        else
            std::snprintf(lineA, sizeof(lineA), "%.2f ms", pass.cpuTimeMs);

        drawList->AddText(ImVec2(node.rect.Min.x + 12.0f, node.rect.Min.y + 32.0f), ColorU32(0.66f, 0.74f, 0.85f, 1.0f),
                          lineA);
        if (zoom > 0.75f)
        {
            const char* chip = pass.hasSideEffects ? "sidefx" : (pass.culled ? "culled" : "pass");
            const ImVec2 chipPos(node.rect.Max.x - 62.0f, node.rect.Min.y + 12.0f);
            drawList->AddRectFilled(chipPos, ImVec2(chipPos.x + 48.0f, chipPos.y + 18.0f),
                                    ColorU32(0.08f, 0.10f, 0.13f, 0.95f), 6.0f);
            drawList->AddText(ImVec2(chipPos.x + 6.0f, chipPos.y + 2.0f), ColorU32(0.74f, 0.78f, 0.84f, 1.0f), chip);
        }

        ImGui::SetCursorScreenPos(node.rect.Min);
        ImGui::PushID(static_cast<int>(pass.index));
        ImGui::InvisibleButton("PassNode", node.rect.GetSize());
        if (ImGui::IsItemClicked())
        {
            m_RenderGraphSelectionType = RenderGraphSelectionType::Pass;
            m_SelectedRenderGraphPass = pass.index;
            m_SelectedRenderGraphResource = {};
        }
        if (ImGui::IsItemHovered())
        {
            ImGui::BeginTooltip();
            ImGui::Text("%s", pass.name.c_str());
            ImGui::Text("Reads: %zu  Writes: %zu  Creates: %zu", pass.reads.size(), pass.writes.size(),
                        pass.creates.size());
            ImGui::Text("Barriers: %zu", pass.barriers.size());
            ImGui::EndTooltip();
        }
        ImGui::PopID();
    }

    for (const RenderGraphResourceNode& node : resourceNodes)
    {
        const FrameGraphResource* resource = m_FrameGraph->GetResource(node.handle);
        if (!resource)
            continue;
        const bool isOutput = ContainsHandle(outputs, node.handle);
        const bool selected = m_RenderGraphSelectionType == RenderGraphSelectionType::Resource &&
                              m_SelectedRenderGraphResource == node.handle;
        const ImVec4 fill = ResourceFillColor(*resource, isOutput);
        drawList->AddRectFilled(node.rect.Min, node.rect.Max, ImGui::ColorConvertFloat4ToU32(fill), 8.0f);
        drawList->AddRect(node.rect.Min, node.rect.Max,
                          selected ? ColorU32(0.94f, 0.86f, 0.40f, 1.0f) : ColorU32(0.40f, 0.45f, 0.54f, 1.0f), 8.0f,
                          0, selected ? 3.0f : 1.5f);

        char topLine[24];
        char bottomLine[48];
        std::snprintf(topLine, sizeof(topLine), "v%u", resource->version);
        std::snprintf(bottomLine, sizeof(bottomLine), "%s",
                      FormatBytes(EstimateResourceBytes(resource->desc)).c_str());
        drawList->AddText(ImVec2(node.rect.Min.x + 10.0f, node.rect.Min.y + 9.0f), ColorU32(0.95f, 0.95f, 0.98f, 1.0f),
                          topLine);
        if (zoom > 0.75f)
        {
            drawList->AddText(ImVec2(node.rect.Min.x + 10.0f, node.rect.Min.y + 24.0f),
                              ColorU32(0.70f, 0.76f, 0.86f, 1.0f), bottomLine);
        }

        char slotLine[48];
        std::snprintf(slotLine, sizeof(slotLine), "P%d / #%d",
                      resource->producerPass == UINT32_MAX ? -1 : static_cast<int>(resource->producerPass),
                      resource->physicalIndex);
        if (zoom > 0.75f)
        {
            drawList->AddText(ImVec2(node.rect.Min.x + 10.0f, node.rect.Min.y + 36.0f),
                              ColorU32(0.52f, 0.58f, 0.66f, 1.0f), slotLine);
        }

        ImGui::SetCursorScreenPos(node.rect.Min);
        ImGui::PushID(static_cast<int>(node.handle.index));
        ImGui::InvisibleButton("ResourceNode", node.rect.GetSize());
        const bool itemHovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
        const bool openTextureViewer = itemHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
                                       ImGui::GetIO().MouseClickedCount[ImGuiMouseButton_Left] >= 2 &&
                                       CanPreviewFrameGraphTexture(resource) && m_TextureViewerPanel;
        if (ImGui::IsItemClicked())
        {
            m_RenderGraphSelectionType = RenderGraphSelectionType::Resource;
            m_SelectedRenderGraphResource = node.handle;
            m_SelectedRenderGraphPass = UINT32_MAX;
        }
        if (itemHovered)
        {
            ImGui::BeginTooltip();
            ImGui::Text("%s", resource->desc.name.c_str());
            ImGui::Text("Logical %u  Version %u", resource->logicalId, resource->version);
            ImGui::Text("Lifetime %u -> %u", resource->firstPass, resource->lastPass);
            ImGui::Text("State %s -> %s", ToString(resource->initialState), ToString(resource->finalState));
            if (CanPreviewFrameGraphTexture(resource))
                ImGui::TextDisabled("Double-click to open in Texture Viewer");
            ImGui::EndTooltip();
        }
        if (openTextureViewer)
        {
            m_TextureViewerPanel->OpenRuntimeTexture(resource->desc.name, resource->texture, "Render Graph Resource");
            m_TextureViewerPanel->SetOpen(true);
        }
        ImGui::PopID();
    }

    const ImVec2 legendBase(canvasRect.Min.x + 14.0f, canvasRect.Max.y - 68.0f);
    drawList->AddText(legendBase, ColorU32(0.74f, 0.78f, 0.84f, 1.0f), "Legend");
    drawList->AddLine(ImVec2(legendBase.x, legendBase.y + 24.0f), ImVec2(legendBase.x + 22.0f, legendBase.y + 24.0f),
                      ColorU32(0.36f, 0.67f, 0.98f, 0.88f), 2.0f);
    drawList->AddText(ImVec2(legendBase.x + 28.0f, legendBase.y + 16.0f), ColorU32(0.68f, 0.72f, 0.80f, 1.0f), "Read");
    drawList->AddLine(ImVec2(legendBase.x + 100.0f, legendBase.y + 24.0f), ImVec2(legendBase.x + 122.0f, legendBase.y + 24.0f),
                      ColorU32(0.96f, 0.69f, 0.32f, 0.92f), 2.0f);
    drawList->AddText(ImVec2(legendBase.x + 128.0f, legendBase.y + 16.0f), ColorU32(0.68f, 0.72f, 0.80f, 1.0f),
                      "Write");
    drawList->AddText(ImVec2(legendBase.x + 212.0f, legendBase.y + 16.0f), ColorU32(0.68f, 0.72f, 0.80f, 1.0f),
                      "Gold outline = selection/output");
    drawList->PopClipRect();

    ImGui::EndChild();
    ImGui::PopStyleColor();
}

void DebugPanel::DrawRenderGraphDetails(float height)
{
    DrawSectionHeader("Inspector", "live details for the selected node");

    ImGui::BeginChild("RenderGraphDetails", ImVec2(0.0f, height), true);

    if (m_RenderGraphSelectionType == RenderGraphSelectionType::Pass && m_SelectedRenderGraphPass < m_FrameGraph->GetPasses().size())
    {
        const FrameGraphPass& pass = m_FrameGraph->GetPasses()[m_SelectedRenderGraphPass];
        ImGui::Text("%s", pass.name.c_str());
        ImGui::TextDisabled("Pass %u", pass.index);
        ImGui::Separator();
        ImGui::Text("Execution: %s", pass.culled ? "culled" : "active");
        if (!pass.culled)
            ImGui::Text("Execution Index: %u", pass.executionIndex);
        ImGui::Text("CPU: %.3f ms", pass.cpuTimeMs);
        if (pass.gpuTimeMs > 0.0)
            ImGui::Text("GPU: %.3f ms (prev)", pass.gpuTimeMs);
        ImGui::Text("Reads: %zu  Writes: %zu  Creates: %zu", pass.reads.size(), pass.writes.size(), pass.creates.size());
        ImGui::Text("Barriers: %zu", pass.barriers.size());
        if (pass.hasSideEffects)
            ImGui::TextColored(ImVec4(0.95f, 0.82f, 0.40f, 1.0f), "Has side effects");

        if (!pass.dependencies.empty())
        {
            ImGui::Spacing();
            ImGui::Text("Dependencies");
            for (uint32 dependency : pass.dependencies)
            {
                const FrameGraphPass* depPass =
                    dependency < m_FrameGraph->GetPasses().size() ? &m_FrameGraph->GetPasses()[dependency] : nullptr;
                ImGui::BulletText("%s (%u)", depPass ? depPass->name.c_str() : "Unknown", dependency);
            }
        }

        if (!pass.resourceUses.empty())
        {
            ImGui::Spacing();
            ImGui::Text("Resource Uses");
            for (const FrameGraphResourceUse& use : pass.resourceUses)
            {
                const FrameGraphResource* resource = m_FrameGraph->GetResource(use.handle);
                if (!resource)
                    continue;
                ImGui::BulletText("%s v%u  %s / %s / %s", resource->desc.name.c_str(), resource->version,
                                  ToString(use.access), ToString(use.usage), ToString(use.state));
            }
        }

        if (!pass.barriers.empty())
        {
            ImGui::Spacing();
            ImGui::Text("Barriers");
            for (const FrameGraphBarrier& barrier : pass.barriers)
            {
                const FrameGraphResource* resource = m_FrameGraph->GetResource(barrier.handle);
                ImGui::BulletText("%s: %s -> %s", resource ? resource->desc.name.c_str() : "Unknown",
                                  ToString(barrier.before), ToString(barrier.after));
            }
        }
    }
    else if (m_RenderGraphSelectionType == RenderGraphSelectionType::Resource && m_SelectedRenderGraphResource.IsValid())
    {
        const FrameGraphResource* resource = m_FrameGraph->GetResource(m_SelectedRenderGraphResource);
        if (resource)
        {
            ImGui::Text("%s", resource->desc.name.c_str());
            ImGui::TextDisabled("Logical %u / Version %u", resource->logicalId, resource->version);
            ImGui::Separator();
            ImGui::Text("Type: %s", ToString(resource->desc.type));
            ImGui::Text("Imported: %s", resource->imported ? "Yes" : "No");
            ImGui::Text("Estimated Memory: %s", FormatBytes(EstimateResourceBytes(resource->desc)).c_str());
            if (CanPreviewFrameGraphTexture(resource) && m_TextureViewerPanel)
            {
                if (ImGui::Button("Open In Texture Viewer"))
                {
                    m_TextureViewerPanel->OpenRuntimeTexture(resource->desc.name, resource->texture,
                                                             "Render Graph Resource");
                    m_TextureViewerPanel->SetOpen(true);
                }
            }
            ImGui::Text("Producer: %s",
                        resource->producerPass == UINT32_MAX ? "None"
                                                             : m_FrameGraph->GetPasses()[resource->producerPass].name.c_str());
            ImGui::Text("Lifetime: %u -> %u", resource->firstPass, resource->lastPass);
            ImGui::Text("Physical Index: %d", resource->physicalIndex);
            ImGui::Text("State: %s -> %s", ToString(resource->initialState), ToString(resource->finalState));
            if (resource->parent.IsValid())
            {
                const FrameGraphResource* parent = m_FrameGraph->GetResource(resource->parent);
                ImGui::Text("Parent Version: %s v%u", parent ? parent->desc.name.c_str() : "Unknown",
                            parent ? parent->version : 0u);
            }

            ImGui::Spacing();
            ImGui::Text("Descriptor");
            if (resource->desc.type == FrameGraphResourceType::Texture)
            {
                ImGui::BulletText("%ux%u  layers %u  mips %u", resource->desc.width, resource->desc.height,
                                  resource->desc.arrayLayers, resource->desc.mipLevels);
                ImGui::BulletText("Format %d  initial %s", static_cast<int>(resource->desc.format),
                                  ToString(resource->desc.initialState));
            }
            else
            {
                ImGui::BulletText("Size %llu  stride %llu", static_cast<unsigned long long>(resource->desc.bufferSize),
                                  static_cast<unsigned long long>(resource->desc.bufferStride));
            }

            if (!resource->consumerPasses.empty())
            {
                ImGui::Spacing();
                ImGui::Text("Consumers");
                for (uint32 consumer : resource->consumerPasses)
                {
                    const FrameGraphPass* consumerPass =
                        consumer < m_FrameGraph->GetPasses().size() ? &m_FrameGraph->GetPasses()[consumer] : nullptr;
                    ImGui::BulletText("%s (%u)", consumerPass ? consumerPass->name.c_str() : "Unknown", consumer);
                }
            }
        }
    }
    else
    {
        ImGui::TextDisabled("Nothing selected");
        ImGui::Spacing();
        ImGui::BulletText("Click a pass node to inspect timings, dependencies, barriers, and resource usage.");
        ImGui::BulletText("Click a resource node to inspect lifetime, versioning, physical reuse, and state.");
        ImGui::BulletText("The canvas shows pass order on top and resource versions below.");
        ImGui::Separator();
        ImGui::Text("Outputs");
        for (FrameGraphResourceHandle output : m_FrameGraph->GetOutputs())
        {
            const FrameGraphResource* resource = m_FrameGraph->GetResource(output);
            if (resource)
                ImGui::BulletText("%s v%u", resource->desc.name.c_str(), resource->version);
        }
    }

    if (ImGui::CollapsingHeader("Raw Lists"))
    {
        if (ImGui::TreeNode("Pass List"))
        {
            for (const FrameGraphPass& pass : m_FrameGraph->GetPasses())
                ImGui::BulletText("%u: %s%s", pass.index, pass.name.c_str(), pass.culled ? " [culled]" : "");
            ImGui::TreePop();
        }
        if (ImGui::TreeNode("Resource List"))
        {
            for (const FrameGraphResource& resource : m_FrameGraph->GetResources())
                ImGui::BulletText("%s v%u [L%u / P%d]", resource.desc.name.c_str(), resource.version, resource.logicalId,
                                  resource.producerPass == UINT32_MAX ? -1 : static_cast<int>(resource.producerPass));
            ImGui::TreePop();
        }
    }

    ImGui::EndChild();
}

void DebugPanel::DrawEntityList()
{
    if (!m_World)
    {
        ImGui::Text("No world set");
        return;
    }

    ImGui::BeginChild("EntityList", ImVec2(0, 200), true);

    int index = 0;
    m_World->EachEntity(
        [this, &index](Entity entity)
        {
            auto* name = m_World->GetComponent<NameComponent>(entity);
            auto* transform = m_World->GetComponent<TransformComponent>(entity);
            auto* primitive = m_World->GetComponent<PrimitiveComponent>(entity);

            ImGui::PushID(index++);

            bool hasTransform = transform != nullptr;
            bool hasPrimitive = primitive != nullptr;

            // Color code based on components
            ImVec4 color = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
            if (!hasTransform)
                color = ImVec4(1.0f, 0.5f, 0.5f, 1.0f); // Red if missing transform
            else if (!hasPrimitive)
                color = ImVec4(0.5f, 0.5f, 1.0f, 1.0f); // Blue if folder/no primitive

            ImGui::TextColored(color, "[%d:%d] %s", entity.GetIndex(), entity.GetGeneration(),
                               name ? name->name.c_str() : "<unnamed>");

            if (hasTransform && ImGui::IsItemHovered())
            {
                ImGui::BeginTooltip();
                ImGui::Text("Position: (%.2f, %.2f, %.2f)", transform->position.x, transform->position.y,
                            transform->position.z);
                const float* mat = transform->worldMatrix.Data();
                ImGui::Text("World Matrix [0]: %.2f, %.2f, %.2f, %.2f", mat[0], mat[1], mat[2], mat[3]);
                ImGui::EndTooltip();
            }

            ImGui::PopID();
        });

    ImGui::EndChild();
}

void DebugPanel::DrawArchetypeInfo()
{
    if (!m_World)
    {
        ImGui::Text("No world set");
        return;
    }

    usize archetypeCount = m_World->GetArchetypeCount();
    ImGui::Text("Total Archetypes: %zu", archetypeCount);
    ImGui::Separator();

    if (archetypeCount == 0)
    {
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "No archetypes created yet");
        return;
    }

    ImGui::BeginChild("ArchetypeList", ImVec2(0, 200), true);

    int index = 0;
    m_World->EachArchetype(
        [&index](Archetype& archetype)
        {
            const auto& signature = archetype.GetSignature();
            usize entityCount = archetype.GetEntityCount();
            usize componentCount = signature.GetTypeCount();

            ImGui::PushID(index++);

            // Archetype header with entity count
            bool open =
                ImGui::TreeNode("", "Archetype #%d (%zu entities, %zu components)", index, entityCount, componentCount);

            if (open)
            {
                // Show component types
                const auto& types = signature.GetTypes();
                ImGui::Indent();
                for (ComponentTypeId typeId : types)
                {
                    // We can show the type ID, but getting the name requires registry lookup
                    ImGui::BulletText("Component ID: %u", typeId);
                }
                ImGui::Unindent();

                // Show first few entity IDs if any
                if (entityCount > 0)
                {
                    ImGui::Separator();
                    ImGui::Text("Entities:");
                    const auto& entities = archetype.GetEntities();
                    usize showCount = (std::min)(entityCount, (usize)5);
                    for (usize i = 0; i < showCount; ++i)
                    {
                        ImGui::Text("  [%u:%u]", entities[i].GetIndex(), entities[i].GetGeneration());
                    }
                    if (entityCount > 5)
                    {
                        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "  ... and %zu more", entityCount - 5);
                    }
                }

                ImGui::TreePop();
            }

            ImGui::PopID();
        });

    ImGui::EndChild();
}

void DebugPanel::DrawMemoryStats()
{
    auto& memSys = MemorySystem::Get();

    if (!memSys.IsInitialized())
    {
        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "Memory System not initialized");
        return;
    }

    // Frame allocator stats
    ImGui::Text("Frame Allocator:");
    usize frameUsed = memSys.GetFrameAllocatorUsed();
    usize frameCapacity = memSys.GetFrameAllocatorCapacity();
    float framePercent = frameCapacity > 0 ? (float)frameUsed / (float)frameCapacity : 0.0f;

    // Color based on usage
    ImVec4 barColor = ImVec4(0.2f, 0.8f, 0.2f, 1.0f); // Green
    if (framePercent > 0.7f)
        barColor = ImVec4(1.0f, 1.0f, 0.0f, 1.0f); // Yellow
    if (framePercent > 0.9f)
        barColor = ImVec4(1.0f, 0.3f, 0.3f, 1.0f); // Red

    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, barColor);
    char overlay[64];
    snprintf(overlay, sizeof(overlay), "%.1f KB / %.1f MB (%.1f%%)", frameUsed / 1024.0f,
             frameCapacity / (1024.0f * 1024.0f), framePercent * 100.0f);
    ImGui::ProgressBar(framePercent, ImVec2(-1, 0), overlay);
    ImGui::PopStyleColor();

    // Scratch allocator stats
    if (auto* scratch = memSys.GetScratchAllocator())
    {
        ImGui::Separator();
        ImGui::Text("Scratch Allocator:");
        usize scratchUsed = scratch->GetUsedMemory();
        usize scratchCapacity = scratch->GetCapacity();
        float scratchPercent = scratchCapacity > 0 ? (float)scratchUsed / (float)scratchCapacity : 0.0f;

        ImVec4 scratchColor = ImVec4(0.2f, 0.6f, 0.8f, 1.0f); // Blue
        if (scratchPercent > 0.7f)
            scratchColor = ImVec4(1.0f, 1.0f, 0.0f, 1.0f);
        if (scratchPercent > 0.9f)
            scratchColor = ImVec4(1.0f, 0.3f, 0.3f, 1.0f);

        ImGui::PushStyleColor(ImGuiCol_PlotHistogram, scratchColor);
        snprintf(overlay, sizeof(overlay), "%.1f KB / %.1f MB (%.1f%%)", scratchUsed / 1024.0f,
                 scratchCapacity / (1024.0f * 1024.0f), scratchPercent * 100.0f);
        ImGui::ProgressBar(scratchPercent, ImVec2(-1, 0), overlay);
        ImGui::PopStyleColor();
    }

    ImGui::Separator();
    ImGui::Text("Frame: %llu", (unsigned long long)memSys.GetFrameNumber());
}

} // namespace Dot
