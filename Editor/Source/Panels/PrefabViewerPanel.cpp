// =============================================================================
// Dot Engine - Prefab Viewer Panel Implementation
// =============================================================================

#include "PrefabViewerPanel.h"
#include "PanelChrome.h"

#include <imgui.h>

namespace Dot
{

// Icons
#define ICON_PREFAB "\xef\x86\xac"    // fa-cube
#define ICON_ENTITY "\xef\x83\xa0"    // fa-sitemap
#define ICON_COMPONENT "\xef\x84\xa9" // fa-puzzle-piece

PrefabViewerPanel::PrefabViewerPanel() : EditorPanel("Prefab Viewer")
{
    m_Open = false;
}

void PrefabViewerPanel::OpenPrefab(const std::filesystem::path& path)
{
    m_FilePath = path;
    m_SelectedEntityIndex = -1;

    if (!m_Prefab.LoadFromFile(path.string()))
    {
        m_FilePath.clear();
    }
}

void PrefabViewerPanel::ClosePrefab()
{
    m_FilePath.clear();
    m_SelectedEntityIndex = -1;
    // Create a new empty prefab
    m_Prefab = Prefab();
}

void PrefabViewerPanel::OnImGui()
{
    if (!m_Open)
        return;

    ImGui::SetNextWindowSize(ImVec2(450, 600), ImGuiCond_FirstUseEver);

    std::string title = m_FilePath.empty() ? "Prefab Viewer###PrefabViewer"
                                           : (m_FilePath.filename().string() + " - Prefab Viewer###PrefabViewer");

    if (BeginChromeWindow(title.c_str(), &m_Open))
    {
        if (!HasOpenPrefab())
        {
            ImGui::TextDisabled("No prefab loaded.");
            ImGui::TextDisabled("Double-click a .prefab file in the Asset Manager to view it.");
            ImGui::End();
            return;
        }

        // Header with prefab info
        ImGui::Text("%s %s", ICON_PREFAB, m_Prefab.GetName().c_str());
        ImGui::SameLine(ImGui::GetWindowWidth() - 100);
        if (ImGui::Button("Close"))
        {
            ClosePrefab();
            ImGui::End();
            return;
        }

        ImGui::Separator();
        ImGui::Text("Path: %s", m_FilePath.string().c_str());
        ImGui::Text("Entities: %zu", m_Prefab.GetEntityCount());
        ImGui::Separator();

        // Two-column layout: entity tree on left, components on right
        float panelWidth = ImGui::GetContentRegionAvail().x;

        // Entity Tree (left side)
        ImGui::BeginChild("EntityTree", ImVec2(panelWidth * 0.35f, 0), true);
        ImGui::Text("Entities");
        ImGui::Separator();

        const auto& entities = m_Prefab.GetEntities();
        for (size_t i = 0; i < entities.size(); i++)
        {
            DrawEntityNode(i, entities[i]);
        }

        ImGui::EndChild();

        ImGui::SameLine();

        // Component Inspector (right side)
        ImGui::BeginChild("ComponentInspector", ImVec2(0, 0), true);
        ImGui::Text("Components");
        ImGui::Separator();

        if (m_SelectedEntityIndex >= 0 && m_SelectedEntityIndex < static_cast<int>(entities.size()))
        {
            const PrefabEntity& entity = entities[m_SelectedEntityIndex];
            ImGui::Text("%s %s", ICON_ENTITY, entity.name.c_str());
            ImGui::Spacing();

            for (const auto& [typeName, jsonData] : entity.components)
            {
                DrawComponentData(typeName, jsonData);
            }
        }
        else
        {
            ImGui::TextDisabled("Select an entity to view its components.");
        }

        ImGui::EndChild();
    }
    ImGui::End();
}

void PrefabViewerPanel::DrawEntityNode(size_t index, const PrefabEntity& entity)
{
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;

    if (m_SelectedEntityIndex == static_cast<int>(index))
    {
        flags |= ImGuiTreeNodeFlags_Selected;
    }

    ImGui::TreeNodeEx((void*)(uintptr_t)index, flags, "%s %s", ICON_ENTITY, entity.name.c_str());

    if (ImGui::IsItemClicked())
    {
        m_SelectedEntityIndex = static_cast<int>(index);
    }
}

void PrefabViewerPanel::DrawComponentData(const std::string& typeName, const std::string& jsonData)
{
    if (ImGui::CollapsingHeader((ICON_COMPONENT + std::string(" ") + typeName).c_str(), ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::Indent();

        // Parse and display component data based on type
        if (typeName == "TransformComponent")
        {
            Vec3 pos = ParseVec3(jsonData, "position");
            Vec3 rot = ParseVec3(jsonData, "rotation");
            Vec3 scale = ParseVec3(jsonData, "scale");

            ImGui::Text("Position: (%.2f, %.2f, %.2f)", pos.x, pos.y, pos.z);
            ImGui::Text("Rotation: (%.2f, %.2f, %.2f)", rot.x, rot.y, rot.z);
            ImGui::Text("Scale: (%.2f, %.2f, %.2f)", scale.x, scale.y, scale.z);
        }
        else if (typeName == "PrimitiveComponent")
        {
            int type = ParseInt(jsonData, "type");
            const char* typeNames[] = {"Cube", "Sphere", "Cylinder", "Plane", "Cone", "Capsule"};
            ImGui::Text("Type: %s", type < 6 ? typeNames[type] : "Unknown");
            const bool overrideLodThresholds = ParseBool(jsonData, "overrideLodThresholds");
            ImGui::Text("Custom LOD: %s", overrideLodThresholds ? "Yes" : "No");
            if (overrideLodThresholds)
            {
                ImGui::Text("LOD1 Screen Height: %.3f", ParseFloat(jsonData, "lod1ScreenHeight"));
                ImGui::Text("LOD2 Screen Height: %.3f", ParseFloat(jsonData, "lod2ScreenHeight"));
            }
        }
        else if (typeName == "CameraComponent")
        {
            ImGui::Text("FOV: %.1f", ParseFloat(jsonData, "fov"));
            ImGui::Text("Near: %.2f", ParseFloat(jsonData, "nearPlane"));
            ImGui::Text("Far: %.1f", ParseFloat(jsonData, "farPlane"));
            ImGui::Text("Active: %s", ParseBool(jsonData, "isActive") ? "Yes" : "No");
        }
        else if (typeName == "DirectionalLightComponent" || typeName == "PointLightComponent" ||
                 typeName == "SpotLightComponent")
        {
            Vec3 color = ParseVec3(jsonData, "color");
            ImGui::Text("Color: (%.2f, %.2f, %.2f)", color.x, color.y, color.z);
            ImGui::Text("Intensity: %.2f", ParseFloat(jsonData, "intensity"));
            if (typeName != "DirectionalLightComponent")
            {
                ImGui::Text("Range: %.1f", ParseFloat(jsonData, "range"));
            }
        }
        else if (typeName == "RigidBodyComponent")
        {
            ImGui::Text("Mass: %.2f", ParseFloat(jsonData, "mass"));
            ImGui::Text("Kinematic: %s", ParseBool(jsonData, "isKinematic") ? "Yes" : "No");
            ImGui::Text("Use Gravity: %s", ParseBool(jsonData, "useGravity") ? "Yes" : "No");
        }
        else if (typeName == "BoxColliderComponent")
        {
            Vec3 size = ParseVec3(jsonData, "size");
            ImGui::Text("Size: (%.2f, %.2f, %.2f)", size.x, size.y, size.z);
        }
        else if (typeName == "SphereColliderComponent")
        {
            ImGui::Text("Radius: %.2f", ParseFloat(jsonData, "radius"));
        }
        else if (typeName == "ScriptComponent")
        {
            ImGui::Text("Script: %s", ParseString(jsonData, "scriptPath").c_str());
        }
        else if (typeName == "CharacterControllerComponent")
        {
            ImGui::Text("Move Speed: %.1f", ParseFloat(jsonData, "moveSpeed"));
            ImGui::Text("Jump Height: %.1f", ParseFloat(jsonData, "jumpHeight"));
            ImGui::Text("Use Gravity: %s", ParseBool(jsonData, "useGravity") ? "Yes" : "No");
        }
        else if (typeName == "NavAgentComponent")
        {
            Vec3 projectionExtent = ParseVec3(jsonData, "projectionExtent");
            ImGui::Text("Move Speed: %.1f", ParseFloat(jsonData, "moveSpeed"));
            ImGui::Text("Stopping Distance: %.2f", ParseFloat(jsonData, "stoppingDistance"));
            ImGui::Text("Projection Extent: (%.1f, %.1f, %.1f)", projectionExtent.x, projectionExtent.y,
                        projectionExtent.z);
        }
        else if (typeName == "PlayerInputComponent")
        {
            ImGui::Text("Mouse Sensitivity: %.2f", ParseFloat(jsonData, "mouseSensitivity"));
            ImGui::Text("Mouse Look: %s", ParseBool(jsonData, "enableMouseLook") ? "Yes" : "No");
        }
        else if (typeName == "HealthComponent")
        {
            ImGui::Text("Current: %.1f", ParseFloat(jsonData, "currentHealth"));
            ImGui::Text("Max: %.1f", ParseFloat(jsonData, "maxHealth"));
            ImGui::Text("Invulnerable: %s", ParseBool(jsonData, "invulnerable") ? "Yes" : "No");
            ImGui::Text("Destroy On Death: %s", ParseBool(jsonData, "destroyEntityOnDeath") ? "Yes" : "No");
        }
        else if (typeName == "MaterialComponent")
        {
            std::string path = ParseString(jsonData, "materialPath");
            if (!path.empty())
            {
                ImGui::Text("Material: %s", path.c_str());
            }
            else
            {
                Vec3 color = ParseVec3(jsonData, "baseColor");
                ImGui::Text("Base Color: (%.2f, %.2f, %.2f)", color.x, color.y, color.z);
                ImGui::Text("Metallic: %.2f", ParseFloat(jsonData, "metallic"));
                ImGui::Text("Roughness: %.2f", ParseFloat(jsonData, "roughness"));
            }
        }
        else if (typeName == "MeshComponent")
        {
            ImGui::Text("Mesh: %s", ParseString(jsonData, "meshPath").c_str());
        }
        else if (typeName == "SkyboxComponent")
        {
            ImGui::Text("Cubemap: %s", ParseString(jsonData, "cubemapPath").c_str());
            ImGui::Text("Rotation: %.1f", ParseFloat(jsonData, "rotation"));
        }
        else
        {
            // Unknown component - show raw JSON
            ImGui::TextWrapped("%s", jsonData.c_str());
        }

        ImGui::Unindent();
    }
}

Vec3 PrefabViewerPanel::ParseVec3(const std::string& jsonData, const std::string& key)
{
    Vec3 result(0, 0, 0);
    size_t pos = jsonData.find("\"" + key + "\"");
    if (pos == std::string::npos)
        return result;

    pos = jsonData.find('[', pos);
    if (pos == std::string::npos)
        return result;
    pos++;

    char* end;
    result.x = std::strtof(jsonData.c_str() + pos, &end);
    pos = jsonData.find(',', end - jsonData.c_str()) + 1;
    result.y = std::strtof(jsonData.c_str() + pos, &end);
    pos = jsonData.find(',', end - jsonData.c_str()) + 1;
    result.z = std::strtof(jsonData.c_str() + pos, &end);

    return result;
}

float PrefabViewerPanel::ParseFloat(const std::string& jsonData, const std::string& key)
{
    size_t pos = jsonData.find("\"" + key + "\"");
    if (pos == std::string::npos)
        return 0.0f;
    pos = jsonData.find(':', pos);
    if (pos == std::string::npos)
        return 0.0f;
    return std::strtof(jsonData.c_str() + pos + 1, nullptr);
}

int PrefabViewerPanel::ParseInt(const std::string& jsonData, const std::string& key)
{
    size_t pos = jsonData.find("\"" + key + "\"");
    if (pos == std::string::npos)
        return 0;
    pos = jsonData.find(':', pos);
    if (pos == std::string::npos)
        return 0;
    return std::atoi(jsonData.c_str() + pos + 1);
}

bool PrefabViewerPanel::ParseBool(const std::string& jsonData, const std::string& key)
{
    return jsonData.find("\"" + key + "\": true") != std::string::npos;
}

std::string PrefabViewerPanel::ParseString(const std::string& jsonData, const std::string& key)
{
    size_t pos = jsonData.find("\"" + key + "\"");
    if (pos == std::string::npos)
        return "";
    pos = jsonData.find('"', jsonData.find(':', pos) + 1);
    if (pos == std::string::npos)
        return "";
    pos++;
    size_t endPos = jsonData.find('"', pos);
    if (endPos == std::string::npos)
        return "";
    return jsonData.substr(pos, endPos - pos);
}

} // namespace Dot
