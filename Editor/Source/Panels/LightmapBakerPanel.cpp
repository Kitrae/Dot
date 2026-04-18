// =============================================================================
// Dot Engine - Lightmap Baker Panel
// =============================================================================

#include "LightmapBakerPanel.h"
#include "PanelChrome.h"

#include "../Lightmapping/LightmapBakerSettings.h"
#include "../Map/MapDocument.h"
#include "../Settings/ProjectSettingsStorage.h"
#include "../Toolbox/ToolboxManager.h"

#include "Core/ECS/World.h"
#include "Core/Log.h"
#include "Core/Scene/StaticLightingComponent.h"

#include <imgui.h>

namespace Dot
{

void LightmapBakerPanel::SetContext(World* world, const std::vector<Entity>& selectedEntities, const std::string& scenePath,
                                    MapDocument* mapDocument)
{
    m_World = world;
    m_MapDocument = mapDocument;
    m_SelectedEntities = selectedEntities;
    m_ScenePath = scenePath;
}

void LightmapBakerPanel::OnImGui()
{
    static int s_DebugFrameCount = 0;
    auto logStage = [&](const char* stage)
    {
        if (s_DebugFrameCount < 8)
            DOT_LOG_INFO("LightmapBakerPanel: %s", stage);
    };

    if (!m_Open)
        return;

    ++s_DebugFrameCount;
    logStage("OnImGui begin");

    if (!ToolboxManager::Get().IsLightmapBakerEnabled())
    {
        m_Open = false;
        return;
    }

    logStage("Before Begin");
    BeginChromeWindow(m_Name.c_str(), &m_Open);
    logStage("After Begin");

    if (!m_World)
    {
        ImGui::TextDisabled("No scene available.");
        ImGui::End();
        return;
    }

    const uint64 mapRevision = m_MapDocument ? m_MapDocument->GetRevision() : 0;
    if (m_LastRefreshedWorld != m_World || m_LastRefreshedScenePath != m_ScenePath || m_LastRefreshedMapRevision != mapRevision)
    {
        logStage("Before RefreshBakeStates");
        m_Baker.RefreshBakeStates(*m_World, m_ScenePath, m_MapDocument);
        logStage("After RefreshBakeStates");
        m_LastRefreshedWorld = m_World;
        m_LastRefreshedScenePath = m_ScenePath;
        m_LastRefreshedMapRevision = mapRevision;
    }

    logStage("Before Entity Scan");
    int totalStaticEntities = 0;
    int validBakes = 0;
    int staleBakes = 0;
    int totalStaticBrushFaces = 0;
    int validBrushBakes = 0;
    int staleBrushBakes = 0;
    m_World->EachEntity(
        [&](Entity entity)
        {
            auto* staticLighting = m_World->GetComponent<StaticLightingComponent>(entity);
            if (!staticLighting || !staticLighting->participateInBake)
                return;

            ++totalStaticEntities;
            if (staticLighting->bakeValid)
                ++validBakes;
            if (staticLighting->bakeStale)
                ++staleBakes;
        });
    if (m_MapDocument)
    {
        for (const MapBrush& brush : m_MapDocument->GetAsset().brushes)
        {
            if (!brush.bakedLighting.participateInBake)
                continue;
            totalStaticBrushFaces += static_cast<int>(brush.faces.size());
            for (const MapFace& face : brush.faces)
            {
                if (face.bakedLighting.bakeValid)
                    ++validBrushBakes;
                if (face.bakedLighting.bakeStale)
                    ++staleBrushBakes;
            }
        }
    }
    logStage("After Entity Scan");

    ImGui::Text("Static bake targets: %d", totalStaticEntities);
    ImGui::Text("Valid: %d", validBakes);
    ImGui::Text("Stale: %d", staleBakes);
    if (m_MapDocument)
    {
        ImGui::Text("Brush faces: %d", totalStaticBrushFaces);
        ImGui::Text("Brush valid: %d", validBrushBakes);
        ImGui::Text("Brush stale: %d", staleBrushBakes);
    }
    ImGui::Text("Selection: %d", static_cast<int>(m_SelectedEntities.size()));
    ImGui::Separator();

    auto& settings = LightmapBakerSettings::Get();
    bool settingsDirty = false;
    bool numericSettingsChanged = false;
    logStage("Before Settings Widgets");

    const char* qualityNames[] = {"Preview", "Medium", "High", "Custom"};
    int qualityPreset = static_cast<int>(settings.qualityPreset);
    if (ImGui::Combo("Quality Preset", &qualityPreset, qualityNames, 4))
    {
        const LightmapQualityPreset selectedPreset = static_cast<LightmapQualityPreset>(qualityPreset);
        if (selectedPreset == LightmapQualityPreset::Custom)
            settings.qualityPreset = LightmapQualityPreset::Custom;
        else
            settings.ApplyQualityPreset(selectedPreset);
        settingsDirty = true;
        numericSettingsChanged = selectedPreset != LightmapQualityPreset::Custom;
    }
    ImGui::SameLine();
    if (ImGui::Button("Reset Defaults"))
    {
        settings.ResetToDefaults();
        settingsDirty = true;
    }

    numericSettingsChanged |= ImGui::DragFloat("Texels Per Unit", &settings.texelsPerUnit, 1.0f, 1.0f, 256.0f, "%.0f");
    numericSettingsChanged |= ImGui::SliderInt("Atlas Size", &settings.atlasSize, 256, 4096);
    numericSettingsChanged |= ImGui::SliderInt("Padding", &settings.padding, 0, 32);
    numericSettingsChanged |= ImGui::SliderInt("Dilation Margin", &settings.dilationMargin, 0, 16);
    settingsDirty |= numericSettingsChanged;
    if (numericSettingsChanged)
        settings.SyncQualityPresetFromValues();

    const char* previewNames[] = {"Combined", "Baked Only", "Realtime Only"};
    int previewMode = static_cast<int>(settings.previewMode);
    if (ImGui::Combo("Preview Mode", &previewMode, previewNames, 3))
    {
        settings.previewMode = static_cast<LightmapPreviewMode>(previewMode);
        settingsDirty = true;
    }

    if (settingsDirty)
        ProjectSettingsStorage::Save();
    logStage("After Settings Widgets");

    ImGui::Separator();
    logStage("Before Action Buttons");
    if (ImGui::Button("Bake All", ImVec2(-1.0f, 0.0f)))
        m_Baker.BakeAll(*m_World, m_ScenePath, m_MapDocument);

    if (ImGui::Button("Bake Selected", ImVec2(-1.0f, 0.0f)))
        m_Baker.BakeSelected(*m_World, m_SelectedEntities, m_ScenePath, m_MapDocument);

    if (ImGui::Button("Clear Bake Data", ImVec2(-1.0f, 0.0f)))
        m_Baker.ClearBakeData(*m_World, m_ScenePath, nullptr, m_MapDocument);

    if (ImGui::Button("Open Output Folder", ImVec2(-1.0f, 0.0f)))
        m_Baker.OpenOutputFolder(m_ScenePath);
    logStage("After Action Buttons");

    ImGui::Separator();
    logStage("Before Summary");
    const LightmapBakeSummary& summary = m_Baker.GetLastSummary();
    ImGui::Text("Atlases: %d", summary.atlasCount);
    ImGui::Text("Baked Objects: %d", summary.bakedEntityCount);
    ImGui::Text("Estimated Memory: %.2f MB", static_cast<double>(summary.estimatedBytes) / (1024.0 * 1024.0));
    if (!summary.lastBakeTimestamp.empty())
        ImGui::Text("Last Bake: %s", summary.lastBakeTimestamp.c_str());
    if (!summary.outputFolder.empty())
        ImGui::TextWrapped("Output: %s", summary.outputFolder.c_str());
    if (!summary.warning.empty())
        ImGui::TextColored(ImVec4(0.95f, 0.80f, 0.35f, 1.0f), "%s", summary.warning.c_str());
    if (!summary.error.empty())
        ImGui::TextColored(ImVec4(0.92f, 0.35f, 0.35f, 1.0f), "%s", summary.error.c_str());

    logStage("Before End");
    ImGui::End();
    logStage("After End");
}

} // namespace Dot
