// =============================================================================
// Dot Engine - Project Settings Storage
// =============================================================================

#include "ProjectSettingsStorage.h"

#include "Core/Physics/CollisionLayers.h"
#include "Core/Physics/PhysicsSettings.h"

#include "EditorSettings.h"
#include "../Lightmapping/LightmapBakerSettings.h"
#include "ViewSettings.h"
#include "../Toolbox/ToolboxSettings.h"

#include <array>
#include <cstdint>
#include <fstream>
#include <string>

namespace Dot
{

namespace
{

std::string TrimCopy(const std::string& value)
{
    const size_t first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos)
        return {};

    const size_t last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

bool ParseBool(const std::string& value, bool& outValue)
{
    const std::string trimmed = TrimCopy(value);
    if (trimmed == "1" || trimmed == "true" || trimmed == "True")
    {
        outValue = true;
        return true;
    }

    if (trimmed == "0" || trimmed == "false" || trimmed == "False")
    {
        outValue = false;
        return true;
    }

    return false;
}

bool ParseInt(const std::string& value, int& outValue)
{
    try
    {
        outValue = std::stoi(TrimCopy(value));
        return true;
    }
    catch (...)
    {
        return false;
    }
}

bool ParseUInt(const std::string& value, uint32& outValue)
{
    try
    {
        outValue = static_cast<uint32>(std::stoul(TrimCopy(value)));
        return true;
    }
    catch (...)
    {
        return false;
    }
}

bool ParseFloat(const std::string& value, float& outValue)
{
    try
    {
        outValue = std::stof(TrimCopy(value));
        return true;
    }
    catch (...)
    {
        return false;
    }
}

bool ParseLayerIndexKey(const std::string& key, const char* prefix, int& outIndex)
{
    const std::string prefixString = prefix;
    if (key.rfind(prefixString, 0) != 0)
        return false;

    return ParseInt(key.substr(prefixString.size()), outIndex);
}

void ApplyCollisionMatrix(const std::array<uint32, CollisionLayers::kMaxLayers>& loadedMasks,
                          const std::array<bool, CollisionLayers::kMaxLayers>& hasMask)
{
    auto& layers = CollisionLayers::Get();

    for (uint8 row = 0; row < CollisionLayers::kMaxLayers; ++row)
    {
        for (uint8 col = row; col < CollisionLayers::kMaxLayers; ++col)
        {
            layers.SetLayersCollide(row, col, false);
        }
    }

    for (uint8 row = 0; row < CollisionLayers::kMaxLayers; ++row)
    {
        for (uint8 col = row; col < CollisionLayers::kMaxLayers; ++col)
        {
            bool enabled = false;
            if (hasMask[row] && (loadedMasks[row] & CollisionLayers::LayerBit(col)) != 0)
                enabled = true;
            if (hasMask[col] && (loadedMasks[col] & CollisionLayers::LayerBit(row)) != 0)
                enabled = true;

            if (enabled)
                layers.SetLayersCollide(row, col, true);
        }
    }
}

} // namespace

std::filesystem::path ProjectSettingsStorage::GetFilePath()
{
    return std::filesystem::current_path() / "Config" / "ProjectSettings.ini";
}

bool ProjectSettingsStorage::Load()
{
    const std::filesystem::path filePath = GetFilePath();
    if (!std::filesystem::exists(filePath))
        return false;

    std::ifstream file(filePath);
    if (!file.is_open())
        return false;

    auto& editor = EditorSettings::Get();
    auto& view = ViewSettings::Get();
    auto& lightmap = LightmapBakerSettings::Get();
    auto& physics = PhysicsSettings::Get();
    auto& collisionLayers = CollisionLayers::Get();
    auto& toolbox = ToolboxSettings::Get();
    toolbox.moduleStates.clear();
    toolbox.showAdvancedModules = false;

    std::array<uint32, CollisionLayers::kMaxLayers> loadedMasks = {};
    std::array<bool, CollisionLayers::kMaxLayers> hasMask = {};
    bool loadedDebugVisMode = false;

    std::string section;
    std::string line;
    while (std::getline(file, line))
    {
        const std::string trimmed = TrimCopy(line);
        if (trimmed.empty() || trimmed[0] == ';' || trimmed[0] == '#')
            continue;

        if (trimmed.front() == '[' && trimmed.back() == ']')
        {
            section = trimmed.substr(1, trimmed.size() - 2);
            continue;
        }

        const size_t equalsPos = trimmed.find('=');
        if (equalsPos == std::string::npos)
            continue;

        const std::string key = TrimCopy(trimmed.substr(0, equalsPos));
        const std::string value = TrimCopy(trimmed.substr(equalsPos + 1));

        if (section == "Editor")
        {
            if (key == "ShowGrid")
                ParseBool(value, editor.showGrid);
            else if (key == "GridSize")
                ParseFloat(value, editor.gridSize);
            else if (key == "GridSpacing")
                ParseFloat(value, editor.gridSpacing);
            else if (key == "GridColorR")
                ParseFloat(value, editor.gridColorR);
            else if (key == "GridColorG")
                ParseFloat(value, editor.gridColorG);
            else if (key == "GridColorB")
                ParseFloat(value, editor.gridColorB);
            else if (key == "GridColorA")
                ParseFloat(value, editor.gridColorA);
            else if (key == "ShowAxisIndicator")
                ParseBool(value, editor.showAxisIndicator);
            else if (key == "AxisLength")
                ParseFloat(value, editor.axisLength);
            else if (key == "ShowSelectionGizmo")
                ParseBool(value, editor.showSelectionGizmo);
            else if (key == "ShowLightGizmos")
                ParseBool(value, editor.showLightGizmos);
            else if (key == "ShowCameraFrustums")
                ParseBool(value, editor.showCameraFrustums);
            else if (key == "ShowAttachmentSockets")
                ParseBool(value, editor.showAttachmentSockets);
            else if (key == "ShowNavMeshGizmo")
                ParseBool(value, editor.showNavMeshGizmo);
            else if (key == "LayoutTranslationSnapEnabled")
                ParseBool(value, editor.layoutTranslationSnapEnabled);
            else if (key == "LayoutTranslationSnapStep")
                ParseFloat(value, editor.layoutTranslationSnapStep);
            else if (key == "LayoutRotationSnapEnabled")
                ParseBool(value, editor.layoutRotationSnapEnabled);
            else if (key == "LayoutRotationSnapStep")
                ParseFloat(value, editor.layoutRotationSnapStep);
            else if (key == "LayoutScaleSnapEnabled")
                ParseBool(value, editor.layoutScaleSnapEnabled);
            else if (key == "LayoutScaleSnapStep")
                ParseFloat(value, editor.layoutScaleSnapStep);
            else if (key == "MapTranslationSnapEnabled")
                ParseBool(value, editor.mapTranslationSnapEnabled);
            else if (key == "MapTranslationSnapStep")
                ParseFloat(value, editor.mapTranslationSnapStep);
        }
        else if (section == "View")
        {
            if (key == "DebugVisMode")
            {
                int modeValue = 0;
                if (ParseInt(value, modeValue) && modeValue >= 0 && modeValue < static_cast<int>(DebugVisMode::Count))
                {
                    view.debugVisMode = SanitizeDebugVisMode(static_cast<DebugVisMode>(modeValue));
                    view.SyncLegacyFromDebugVis();
                    loadedDebugVisMode = true;
                }
            }
            else if (key == "RenderMode")
            {
                // Legacy backward compat: preserve the old renderMode field.
                int modeValue = 0;
                if (ParseInt(value, modeValue))
                {
                    if (modeValue == 1)
                        view.renderMode = RenderMode::Wireframe;
                    else if (modeValue == 2)
                        view.renderMode = RenderMode::Depth;
                    else
                        view.renderMode = RenderMode::Normal;
                }
            }
            else if (key == "WireframeOverlay")
            {
                ParseBool(value, view.wireframeOverlay);
            }
            else if (key == "ShadowsEnabled")
            {
                ParseBool(value, view.shadowsEnabled);
            }
            else if (key == "SSAOEnabled")
            {
                ParseBool(value, view.ssaoEnabled);
            }
            else if (key == "AntiAliasingEnabled")
            {
                ParseBool(value, view.antiAliasingEnabled);
            }
            else if (key == "FrustumCullingEnabled")
            {
                ParseBool(value, view.frustumCullingEnabled);
            }
            else if (key == "HZBEnabled")
            {
                ParseBool(value, view.hzbEnabled);
            }
            else if (key == "ForwardPlusEnabled")
            {
                ParseBool(value, view.forwardPlusEnabled);
            }
            else if (key == "LodDebugTint")
            {
                ParseBool(value, view.lodDebugTint);
            }
            else if (key == "LodAggressiveness")
            {
                ParseFloat(value, view.lodAggressiveness);
            }
            else if (key == "SSAODebugFullscreen")
            {
                // Retired legacy fullscreen toggle: intentionally ignored.
            }
            else if (key == "SSAORadius")
            {
                ParseFloat(value, view.ssaoRadius);
            }
            else if (key == "SSAOBias")
            {
                ParseFloat(value, view.ssaoBias);
            }
            else if (key == "SSAOIntensity")
            {
                ParseFloat(value, view.ssaoIntensity);
            }
            else if (key == "SSAOPower")
            {
                ParseFloat(value, view.ssaoPower);
            }
            else if (key == "SSAOThickness")
            {
                ParseFloat(value, view.ssaoThickness);
            }
            else if (key == "SSAOMaxScreenRadius")
            {
                ParseFloat(value, view.ssaoMaxScreenRadius);
            }
            else if (key == "SSAOBlurDepthThreshold")
            {
                ParseFloat(value, view.ssaoBlurDepthThreshold);
            }
            else if (key == "SSAOSampleCount")
            {
                ParseInt(value, view.ssaoSampleCount);
            }
            else if (key == "SSAOHalfResolution")
            {
                ParseBool(value, view.ssaoHalfResolution);
            }
            else if (key == "SSAOPreferExternalShaders")
            {
                ParseBool(value, view.ssaoPreferExternalShaders);
            }
        }
        else if (section == "Physics")
        {
            if (key == "FixedTimestep")
                ParseFloat(value, physics.fixedTimestep);
            else if (key == "MaxSubSteps")
                ParseInt(value, physics.maxSubSteps);
            else if (key == "GravityX")
                ParseFloat(value, physics.gravity.x);
            else if (key == "GravityY")
                ParseFloat(value, physics.gravity.y);
            else if (key == "GravityZ")
                ParseFloat(value, physics.gravity.z);
            else if (key == "PositionCorrectionPercent")
                ParseFloat(value, physics.positionCorrectionPercent);
            else if (key == "PositionCorrectionSlop")
                ParseFloat(value, physics.positionCorrectionSlop);
            else if (key == "ShowColliders")
                ParseBool(value, physics.showColliders);
            else if (key == "ShowContactPoints")
                ParseBool(value, physics.showContactPoints);
        }
        else if (section == "LightmapBaker")
        {
            if (key == "TexelsPerUnit")
            {
                ParseFloat(value, lightmap.texelsPerUnit);
            }
            else if (key == "AtlasSize")
            {
                ParseInt(value, lightmap.atlasSize);
            }
            else if (key == "Padding")
            {
                ParseInt(value, lightmap.padding);
            }
            else if (key == "DilationMargin")
            {
                ParseInt(value, lightmap.dilationMargin);
            }
            else if (key == "QualityPreset")
            {
                int preset = 0;
                if (ParseInt(value, preset))
                    lightmap.qualityPreset = static_cast<LightmapQualityPreset>(preset);
            }
            else if (key == "PreviewMode")
            {
                int previewMode = 0;
                if (ParseInt(value, previewMode))
                    lightmap.previewMode = static_cast<LightmapPreviewMode>(previewMode);
            }
        }
        else if (section == "CollisionLayers")
        {
            int index = -1;
            if (ParseLayerIndexKey(key, "LayerName", index))
            {
                if (index >= 0 && index < CollisionLayers::kMaxLayers)
                    collisionLayers.SetLayerName(static_cast<uint8>(index), value);
            }
            else if (ParseLayerIndexKey(key, "LayerMask", index))
            {
                uint32 parsedMask = 0;
                if (index >= 0 && index < CollisionLayers::kMaxLayers && ParseUInt(value, parsedMask))
                {
                    loadedMasks[static_cast<size_t>(index)] = parsedMask;
                    hasMask[static_cast<size_t>(index)] = true;
                }
            }
        }
        else if (section == "Toolbox")
        {
            if (key == "ShowAdvancedModules")
            {
                ParseBool(value, toolbox.showAdvancedModules);
            }
            else if (key.rfind("Module.", 0) == 0)
            {
                bool enabled = false;
                if (ParseBool(value, enabled))
                    toolbox.moduleStates[key.substr(7)] = enabled;
            }
        }
    }

    bool anyMaskLoaded = false;
    for (bool present : hasMask)
    {
        if (present)
        {
            anyMaskLoaded = true;
            break;
        }
    }

    if (anyMaskLoaded)
        ApplyCollisionMatrix(loadedMasks, hasMask);

    if (loadedDebugVisMode)
        view.SyncLegacyFromDebugVis();
    else
        view.SyncDebugVisFromLegacy();

    lightmap.SyncQualityPresetFromValues();

    return true;
}

bool ProjectSettingsStorage::Save()
{
    const std::filesystem::path filePath = GetFilePath();
    std::filesystem::create_directories(filePath.parent_path());

    std::ofstream file(filePath, std::ios::trunc);
    if (!file.is_open())
        return false;

    const auto& editor = EditorSettings::Get();
    const auto& view = ViewSettings::Get();
    const auto& lightmap = LightmapBakerSettings::Get();
    const auto& physics = PhysicsSettings::Get();
    const auto& collisionLayers = CollisionLayers::Get();
    const auto& toolbox = ToolboxSettings::Get();

    file << "[Editor]\n";
    file << "ShowGrid=" << (editor.showGrid ? 1 : 0) << '\n';
    file << "GridSize=" << editor.gridSize << '\n';
    file << "GridSpacing=" << editor.gridSpacing << '\n';
    file << "GridColorR=" << editor.gridColorR << '\n';
    file << "GridColorG=" << editor.gridColorG << '\n';
    file << "GridColorB=" << editor.gridColorB << '\n';
    file << "GridColorA=" << editor.gridColorA << '\n';
    file << "ShowAxisIndicator=" << (editor.showAxisIndicator ? 1 : 0) << '\n';
    file << "AxisLength=" << editor.axisLength << '\n';
    file << "ShowSelectionGizmo=" << (editor.showSelectionGizmo ? 1 : 0) << '\n';
    file << "ShowLightGizmos=" << (editor.showLightGizmos ? 1 : 0) << '\n';
    file << "ShowCameraFrustums=" << (editor.showCameraFrustums ? 1 : 0) << '\n';
    file << "ShowAttachmentSockets=" << (editor.showAttachmentSockets ? 1 : 0) << '\n';
    file << "ShowNavMeshGizmo=" << (editor.showNavMeshGizmo ? 1 : 0) << '\n';
    file << "LayoutTranslationSnapEnabled=" << (editor.layoutTranslationSnapEnabled ? 1 : 0) << '\n';
    file << "LayoutTranslationSnapStep=" << editor.layoutTranslationSnapStep << '\n';
    file << "LayoutRotationSnapEnabled=" << (editor.layoutRotationSnapEnabled ? 1 : 0) << '\n';
    file << "LayoutRotationSnapStep=" << editor.layoutRotationSnapStep << '\n';
    file << "LayoutScaleSnapEnabled=" << (editor.layoutScaleSnapEnabled ? 1 : 0) << '\n';
    file << "LayoutScaleSnapStep=" << editor.layoutScaleSnapStep << '\n';
    file << "MapTranslationSnapEnabled=" << (editor.mapTranslationSnapEnabled ? 1 : 0) << '\n';
    file << "MapTranslationSnapStep=" << editor.mapTranslationSnapStep << "\n\n";

    file << "[View]\n";
    file << "DebugVisMode=" << static_cast<int>(SanitizeDebugVisMode(view.debugVisMode)) << '\n';
    file << "ShadowsEnabled=" << (view.shadowsEnabled ? 1 : 0) << '\n';
    file << "AntiAliasingEnabled=" << (view.antiAliasingEnabled ? 1 : 0) << '\n';
    file << "FrustumCullingEnabled=" << (view.frustumCullingEnabled ? 1 : 0) << '\n';
    file << "HZBEnabled=" << (view.hzbEnabled ? 1 : 0) << '\n';
    file << "ForwardPlusEnabled=" << (view.forwardPlusEnabled ? 1 : 0) << '\n';
    file << "LodDebugTint=" << (view.lodDebugTint ? 1 : 0) << '\n';
    file << "LodAggressiveness=" << view.lodAggressiveness << '\n';
    file << "SSAOEnabled=" << (view.ssaoEnabled ? 1 : 0) << '\n';
    file << "SSAORadius=" << view.ssaoRadius << '\n';
    file << "SSAOIntensity=" << view.ssaoIntensity << '\n';
    file << "SSAOBlurDepthThreshold=" << view.ssaoBlurDepthThreshold << '\n';
    file << "SSAOSampleCount=" << view.ssaoSampleCount << '\n';
    file << "SSAOHalfResolution=" << (view.ssaoHalfResolution ? 1 : 0) << '\n';
    file << "\n";

    file << "[Physics]\n";
    file << "FixedTimestep=" << physics.fixedTimestep << '\n';
    file << "MaxSubSteps=" << physics.maxSubSteps << '\n';
    file << "GravityX=" << physics.gravity.x << '\n';
    file << "GravityY=" << physics.gravity.y << '\n';
    file << "GravityZ=" << physics.gravity.z << '\n';
    file << "PositionCorrectionPercent=" << physics.positionCorrectionPercent << '\n';
    file << "PositionCorrectionSlop=" << physics.positionCorrectionSlop << '\n';
    file << "ShowColliders=" << (physics.showColliders ? 1 : 0) << '\n';
    file << "ShowContactPoints=" << (physics.showContactPoints ? 1 : 0) << "\n\n";

    file << "[LightmapBaker]\n";
    file << "TexelsPerUnit=" << lightmap.texelsPerUnit << '\n';
    file << "AtlasSize=" << lightmap.atlasSize << '\n';
    file << "Padding=" << lightmap.padding << '\n';
    file << "DilationMargin=" << lightmap.dilationMargin << '\n';
    file << "QualityPreset=" << static_cast<int>(lightmap.qualityPreset) << '\n';
    file << "PreviewMode=" << static_cast<int>(lightmap.previewMode) << "\n\n";

    file << "[Toolbox]\n";
    file << "ShowAdvancedModules=" << (toolbox.showAdvancedModules ? 1 : 0) << '\n';
    for (const auto& [moduleId, enabled] : toolbox.moduleStates)
        file << "Module." << moduleId << "=" << (enabled ? 1 : 0) << '\n';
    file << '\n';

    file << "[CollisionLayers]\n";
    for (uint8 i = 0; i < CollisionLayers::kMaxLayers; ++i)
    {
        file << "LayerName" << static_cast<unsigned>(i) << '=' << collisionLayers.GetLayerName(i) << '\n';
    }
    for (uint8 i = 0; i < CollisionLayers::kMaxLayers; ++i)
    {
        file << "LayerMask" << static_cast<unsigned>(i) << '=' << collisionLayers.GetDefaultCollisionMask(i) << '\n';
    }

    return true;
}

} // namespace Dot
