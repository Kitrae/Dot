#include "SceneSettingsPanel.h"
#include "PanelChrome.h"

#include "../Scene/EditorSceneContext.h"
#include "../Scene/SceneSettingsAsset.h"

#include <cstring>
#include <imgui.h>

namespace Dot
{

void SceneSettingsPanel::OnImGui()
{
    if (!m_Open)
        return;

    BeginChromeWindow(m_Name.c_str(), &m_Open);

    if (!m_SceneContext)
    {
        ImGui::TextDisabled("No scene context");
        ImGui::End();
        return;
    }

    SceneSettingsAsset& settings = m_SceneContext->GetSceneSettings();

    ImGui::SeparatorText("Environment");
    char cubemapPathBuffer[260] = {};
    strncpy_s(cubemapPathBuffer, settings.cubemapPath.c_str(), _TRUNCATE);
    if (ImGui::InputText("Cubemap", cubemapPathBuffer, sizeof(cubemapPathBuffer)))
    {
        settings.cubemapPath = cubemapPathBuffer;
        m_SceneContext->SetSceneDirty(true);
    }
    ImGui::DragFloat("Rotation", &settings.rotation, 1.0f);
    ImGui::ColorEdit3("Tint", &settings.tintR);
    ImGui::Checkbox("Show Markers", &settings.showMarkers);
    m_SceneContext->SetSceneDirty(m_SceneContext->IsSceneDirty() || ImGui::IsItemDeactivatedAfterEdit());

    ImGui::SeparatorText("Ambient");
    if (ImGui::Checkbox("Ambient Enabled", &settings.ambientEnabled))
        m_SceneContext->SetSceneDirty(true);
    if (ImGui::ColorEdit3("Ambient Color", &settings.ambientColorR))
        m_SceneContext->SetSceneDirty(true);
    if (ImGui::DragFloat("Ambient Intensity", &settings.ambientIntensity, 0.01f, 0.0f, 10.0f))
        m_SceneContext->SetSceneDirty(true);

    ImGui::SeparatorText("Sun");
    if (ImGui::Checkbox("Sun Enabled", &settings.sunEnabled))
        m_SceneContext->SetSceneDirty(true);
    if (ImGui::DragFloat2("Sun Rotation", &settings.sunRotationX, 1.0f))
        m_SceneContext->SetSceneDirty(true);
    if (ImGui::ColorEdit3("Sun Color", &settings.sunColorR))
        m_SceneContext->SetSceneDirty(true);
    if (ImGui::DragFloat("Sun Intensity", &settings.sunIntensity, 0.01f, 0.0f, 10.0f))
        m_SceneContext->SetSceneDirty(true);
    if (ImGui::Checkbox("Sun Cast Shadows", &settings.sunCastShadows))
        m_SceneContext->SetSceneDirty(true);
    if (ImGui::DragFloat("Sun Shadow Bias", &settings.sunShadowBias, 0.0001f, 0.0f, 0.1f, "%.5f"))
        m_SceneContext->SetSceneDirty(true);
    if (ImGui::DragFloat("Sun Shadow Distance", &settings.sunShadowDistance, 0.5f, 0.0f, 1000.0f))
        m_SceneContext->SetSceneDirty(true);

    ImGui::SeparatorText("World");
    char mapPathBuffer[260] = {};
    strncpy_s(mapPathBuffer, settings.mapPath.c_str(), _TRUNCATE);
    if (ImGui::InputText("Map Asset", mapPathBuffer, sizeof(mapPathBuffer)))
    {
        settings.mapPath = mapPathBuffer;
        m_SceneContext->SetSceneDirty(true);
    }
    if (ImGui::Checkbox("Map Visible", &settings.mapVisible))
        m_SceneContext->SetSceneDirty(true);
    if (ImGui::Checkbox("Map Collision", &settings.mapCollisionEnabled))
        m_SceneContext->SetSceneDirty(true);

    ImGui::End();
}

} // namespace Dot
