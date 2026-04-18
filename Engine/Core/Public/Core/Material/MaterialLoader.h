// =============================================================================
// Dot Engine - Material Loader
// =============================================================================
// Loads material properties from .dotmat files for rendering.
// =============================================================================

#pragma once

#include "Core/Assets/AssetManager.h"
#include "Core/Log.h"
#include "Core/Material/MaterialTextureUtils.h"
#include "Core/Math/Vec3.h"

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Dot
{

/// Loaded material data for rendering
struct LoadedMaterial
{
    Vec3 baseColor{0.7f, 0.7f, 0.7f};
    float metallic = 0.0f;
    float roughness = 0.5f;
    float ambientOcclusion = 1.0f;

    // Texture data from Texture2D nodes (supports up to 4 slots)
    std::string texturePaths[4];
    int textureSampleTypes[4] = {static_cast<int>(TextureSampleType::Color), static_cast<int>(TextureSampleType::Color),
                                 static_cast<int>(TextureSampleType::Color), static_cast<int>(TextureSampleType::Color)};
    int albedoTextureSlot = -1;
    int normalTextureSlot = -1;
    int ormTextureSlot = -1;
    float tilingU = 1.0f;
    float tilingV = 1.0f;
    float offsetU = 0.0f;
    float offsetV = 0.0f;
    int filterMode = 1; // 0=Point, 1=Bilinear, 2=Trilinear
    int wrapMode = 0;   // 0=Repeat, 1=Clamp, 2=Mirror
    bool hasTextures[4] = {false, false, false, false};

    // Panner animation data
    float pannerSpeedU = 0.0f;
    float pannerSpeedV = 0.0f;
    int pannerMethod = 0; // 0=Linear, 1=Sine, 2=ZigZag, 3=Rotate
    bool pannerLink = false;

    // Custom procedural shader (generated from MaterialGraph)
    std::string customShaderHLSL;
    bool hasCustomShader = false;

    bool valid = false;
};

/// Material loader - parses .dotmat files
class MaterialLoader
{
public:
    static std::string NormalizeTexturePath(const std::string& rawPath)
    {
        if (rawPath.empty())
            return rawPath;

        std::string normalized = rawPath;
        if (!normalized.empty() && normalized.back() == '\r')
            normalized.pop_back();

        const std::filesystem::path texturePath(normalized);
        const std::string assetRoot = AssetManager::Get().GetRootPath();
        if (assetRoot.empty())
            return normalized;

        auto tryRelativeCandidate = [&](const std::filesystem::path& relativeCandidate) -> std::string
        {
            std::error_code ec;
            const std::filesystem::path fullCandidate = std::filesystem::path(assetRoot) / relativeCandidate;
            if (std::filesystem::exists(fullCandidate, ec) && !ec)
                return relativeCandidate.generic_string();
            return {};
        };

        const std::string directRelative = tryRelativeCandidate(texturePath);
        if (!directRelative.empty())
            return directRelative;

        const std::filesystem::path filename = texturePath.filename();
        if (!filename.empty())
        {
            const std::string texturesRelative = tryRelativeCandidate(std::filesystem::path("Textures") / filename);
            if (!texturesRelative.empty())
                return texturesRelative;
        }

        return normalized;
    }

    /// Load material from file path
    static LoadedMaterial Load(const std::string& path)
    {
        LoadedMaterial result;

        std::ifstream file(path);
        if (!file)
        {
            DOT_LOG_ERROR("MaterialLoader: Failed to open material at %s", path.c_str());
            return result;
        }

        std::string line;

        // Check header
        std::getline(file, line);
        if (line.find("DOTMATERIAL") != 0)
            return result;

        // Parse file looking for ConstVec3 (color), ConstFloat (metallic/roughness), and Texture2D values
        std::unordered_map<int, Vec3> colorNodes;
        std::unordered_map<int, float> floatNodes;
        std::unordered_map<int, std::string> textureNodes; // node id -> texture path
        std::unordered_map<int, int> nodeToSlot;           // node id -> slot index
        std::unordered_map<int, int> nodeToSampleType;     // node id -> TextureSampleType enum
        std::unordered_map<int, int> nodeTypes;            // node id -> MaterialNodeType enum value
        std::unordered_map<int, int> pinToNode;
        std::unordered_map<int, int> inputPinToNode;
        std::unordered_map<int, int> inputPinToIndex;
        std::unordered_set<int> nodesWithExplicitSampleType;
        struct ParsedConnection
        {
            int outPin = -1;
            int inPin = -1;
        };
        std::vector<ParsedConnection> parsedConnections;

        int currentNodeType = -1;
        int currentNodeId = -1;

        while (std::getline(file, line))
        {
            std::istringstream iss(line);
            std::string token;
            iss >> token;

            if (token == "NODE")
            {
                int typeInt;
                iss >> typeInt >> currentNodeId;
                currentNodeType = typeInt;
                nodeTypes[currentNodeId] = typeInt;
            }
            else if (token == "VALUE")
            {
                if (currentNodeType == 1) // ConstFloat
                {
                    float val;
                    iss >> val;
                    floatNodes[currentNodeId] = val;
                }
                else if (currentNodeType == 2) // ConstVec3
                {
                    float x, y, z;
                    iss >> x >> y >> z;
                    colorNodes[currentNodeId] = Vec3(x, y, z);
                }
            }
            else if (token == "PATH")
            {
                if (currentNodeType == 4) // Texture2D
                {
                    std::string texPath;
                    std::getline(iss, texPath);
                    size_t start = texPath.find_first_not_of(" \t");
                    if (start != std::string::npos)
                        texPath = texPath.substr(start);
                    textureNodes[currentNodeId] = NormalizeTexturePath(texPath);
                }
            }
            else if (token == "PROP")
            {
                std::string propName;
                iss >> propName;

                if (propName == "Filter")
                {
                    std::string secondary;
                    iss >> secondary;
                    if (secondary == "Mode")
                        propName = "Filter Mode";
                }
                else if (propName == "Wrap")
                {
                    std::string secondary;
                    iss >> secondary;
                    if (secondary == "Mode")
                        propName = "Wrap Mode";
                }
                else if (propName == "Texture")
                {
                    size_t pos = iss.tellg();
                    std::string next;
                    if (iss >> next && next == "Slot")
                        propName = "Texture Slot";
                    else
                    {
                        iss.clear();
                        iss.seekg(pos);
                    }
                }
                else if (propName == "Sample")
                {
                    std::string secondary;
                    iss >> secondary;
                    if (secondary == "Type")
                        propName = "Sample Type";
                }

                if (propName == "Speed" && (currentNodeType == 32 || currentNodeType == 105))
                {
                    float speedU, speedV;
                    iss >> speedU >> speedV;
                    result.pannerSpeedU = speedU;
                    result.pannerSpeedV = speedV;
                }
                else if (propName == "Method" && (currentNodeType == 32 || currentNodeType == 105))
                {
                    iss >> result.pannerMethod;
                }
                else if (propName == "Link" && (currentNodeType == 32 || currentNodeType == 105))
                {
                    int linkVal;
                    if (iss >> linkVal)
                        result.pannerLink = (linkVal != 0);
                }
                else if (propName == "Tiling" && currentNodeType == 4)
                {
                    iss >> result.tilingU >> result.tilingV;
                }
                else if (propName == "Offset" && currentNodeType == 4)
                {
                    iss >> result.offsetU >> result.offsetV;
                }
                else if (propName == "Filter Mode" && currentNodeType == 4)
                {
                    iss >> result.filterMode;
                }
                else if (propName == "Wrap Mode" && currentNodeType == 4)
                {
                    iss >> result.wrapMode;
                }
                else if (propName == "Texture Slot" && currentNodeType == 4)
                {
                    int slot;
                    iss >> slot;
                    nodeToSlot[currentNodeId] = slot;
                }
                else if (propName == "Sample Type" && currentNodeType == 4)
                {
                    int sampleType = static_cast<int>(TextureSampleType::Color);
                    iss >> sampleType;
                    nodeToSampleType[currentNodeId] = sampleType;
                    nodesWithExplicitSampleType.insert(currentNodeId);
                }
                else if ((propName == "Path" || propName == "Texture") && currentNodeType == 4)
                {
                    std::string texPath;
                    iss >> std::ws;
                    std::getline(iss, texPath);
                    textureNodes[currentNodeId] = NormalizeTexturePath(texPath);
                }
            }
            else if (token == "INPUTS")
            {
                int count;
                iss >> count;
                for (int i = 0; i < count; ++i)
                {
                    int pinId;
                    iss >> pinId;
                    inputPinToNode[pinId] = currentNodeId;
                    inputPinToIndex[pinId] = i;
                }
            }
            else if (token == "OUTPUTS")
            {
                int count;
                iss >> count;
                for (int i = 0; i < count; ++i)
                {
                    int pinId;
                    iss >> pinId;
                    pinToNode[pinId] = currentNodeId;
                }
            }
            else if (token == "CONN")
            {
                int connId, outPin, inPin;
                iss >> connId >> outPin >> inPin;
                parsedConnections.push_back({outPin, inPin});
            }
        }

        auto resolveEffectiveTextureSlots = [&]() -> std::unordered_map<int, int>
        {
            struct TextureNodeInfo
            {
                int nodeId = -1;
                int requestedSlot = 0;
                int sampleType = static_cast<int>(TextureSampleType::Color);
                std::string path;
            };

            std::vector<TextureNodeInfo> infos;
            infos.reserve(textureNodes.size());
            for (const auto& [nodeId, texPath] : textureNodes)
            {
                TextureNodeInfo info;
                info.nodeId = nodeId;
                auto slotIt = nodeToSlot.find(nodeId);
                info.requestedSlot = std::clamp(slotIt != nodeToSlot.end() ? slotIt->second : 0, 0, 3);

                auto sampleTypeIt = nodeToSampleType.find(nodeId);
                if (sampleTypeIt != nodeToSampleType.end())
                    info.sampleType = sampleTypeIt->second;
                if (nodesWithExplicitSampleType.find(nodeId) == nodesWithExplicitSampleType.end())
                    info.sampleType = static_cast<int>(GuessTextureSampleTypeFromPath(texPath));
                info.path = texPath;
                infos.push_back(std::move(info));
            }

            auto preferredSlotForType = [](int sampleType) -> int
            {
                if (sampleType == static_cast<int>(TextureSampleType::Normal))
                    return 1;
                if (sampleType == static_cast<int>(TextureSampleType::Mask))
                    return 2;
                return 0;
            };

            std::unordered_map<int, int> resolvedSlots;
            std::array<int, 4> slotOwners = {-1, -1, -1, -1};
            std::array<std::string, 4> slotPaths = {"", "", "", ""};

            auto tryAssign = [&](const TextureNodeInfo& info, int slot) -> bool
            {
                if (slot < 0 || slot >= 4)
                    return false;

                if (slotOwners[slot] == -1 || slotPaths[slot] == info.path)
                {
                    slotOwners[slot] = info.nodeId;
                    slotPaths[slot] = info.path;
                    resolvedSlots[info.nodeId] = slot;
                    return true;
                }
                return false;
            };

            for (const TextureNodeInfo& info : infos)
                tryAssign(info, info.requestedSlot);

            for (const TextureNodeInfo& info : infos)
            {
                if (resolvedSlots.find(info.nodeId) != resolvedSlots.end())
                    continue;

                const int preferredSlot = preferredSlotForType(info.sampleType);
                if (tryAssign(info, preferredSlot))
                    continue;

                for (int slot = 0; slot < 4; ++slot)
                {
                    if (slot == preferredSlot)
                        continue;
                    if (tryAssign(info, slot))
                        break;
                }
            }

            for (const TextureNodeInfo& info : infos)
            {
                if (resolvedSlots.find(info.nodeId) == resolvedSlots.end())
                    resolvedSlots[info.nodeId] = info.requestedSlot;
            }

            return resolvedSlots;
        };

        const std::unordered_map<int, int> effectiveNodeToSlot = resolveEffectiveTextureSlots();

        auto processConnection = [&](int outPin, int inPin)
        {
            auto nodeIt = pinToNode.find(outPin);
            if (nodeIt == pinToNode.end())
                return;

            const int sourceNode = nodeIt->second;
            auto texIt = textureNodes.find(sourceNode);
            if (texIt != textureNodes.end())
            {
                int slot = 0;
                auto slotIt = effectiveNodeToSlot.find(sourceNode);
                if (slotIt != effectiveNodeToSlot.end())
                    slot = slotIt->second;

                if (slot >= 0 && slot < 4)
                {
                    result.texturePaths[slot] = texIt->second;
                    result.hasTextures[slot] = true;
                    int sampleType = static_cast<int>(TextureSampleType::Color);
                    auto sampleTypeIt = nodeToSampleType.find(sourceNode);
                    if (sampleTypeIt != nodeToSampleType.end())
                        sampleType = sampleTypeIt->second;
                    if (nodesWithExplicitSampleType.find(sourceNode) == nodesWithExplicitSampleType.end())
                        sampleType = static_cast<int>(GuessTextureSampleTypeFromPath(texIt->second));
                    result.textureSampleTypes[slot] = sampleType;
                }
            }

            auto targetNodeIt = inputPinToNode.find(inPin);
            auto targetInputIndexIt = inputPinToIndex.find(inPin);
            if (targetNodeIt == inputPinToNode.end() || targetInputIndexIt == inputPinToIndex.end())
                return;

            const int targetNode = targetNodeIt->second;
            const int targetInputIndex = targetInputIndexIt->second;
            auto targetTypeIt = nodeTypes.find(targetNode);
            if (targetTypeIt != nodeTypes.end() && targetTypeIt->second == 0 && texIt != textureNodes.end())
            {
                int slot = 0;
                auto slotIt = effectiveNodeToSlot.find(sourceNode);
                if (slotIt != effectiveNodeToSlot.end())
                    slot = slotIt->second;
                if (slot >= 0 && slot < 4)
                {
                    const bool isNormalSample =
                        result.textureSampleTypes[slot] == static_cast<int>(TextureSampleType::Normal);
                    if (targetInputIndex == 0)
                    {
                        if (isNormalSample)
                            result.normalTextureSlot = slot;
                        else if (result.textureSampleTypes[slot] == static_cast<int>(TextureSampleType::Mask))
                            result.ormTextureSlot = slot;
                        else
                            result.albedoTextureSlot = slot;
                    }
                    else if (targetInputIndex == 3)
                    {
                        result.normalTextureSlot = slot;
                        result.textureSampleTypes[slot] = static_cast<int>(TextureSampleType::Normal);
                    }
                    else if (targetInputIndex == 1 || targetInputIndex == 2 || targetInputIndex == 5)
                    {
                        if (result.textureSampleTypes[slot] == static_cast<int>(TextureSampleType::Mask))
                            result.ormTextureSlot = slot;
                    }
                }
            }
            if (targetTypeIt != nodeTypes.end() && targetTypeIt->second == 0)
            {
                auto colorIt = colorNodes.find(sourceNode);
                if (colorIt != colorNodes.end() && targetInputIndex == 0)
                    result.baseColor = colorIt->second;

                auto floatIt = floatNodes.find(sourceNode);
                if (floatIt != floatNodes.end())
                {
                    if (targetInputIndex == 1)
                        result.metallic = floatIt->second;
                    else if (targetInputIndex == 2)
                        result.roughness = floatIt->second;
                    else if (targetInputIndex == 5)
                        result.ambientOcclusion = floatIt->second;
                }
            }
        };

        for (const ParsedConnection& connection : parsedConnections)
            processConnection(connection.outPin, connection.inPin);

        // Also ensure all textures in textureNodes with a slot assigned are in the result
        for (auto const& [nodeId, texPath] : textureNodes)
        {
            auto slotIt = effectiveNodeToSlot.find(nodeId);
            if (slotIt != effectiveNodeToSlot.end())
            {
                int slot = slotIt->second;
                if (slot >= 0 && slot < 4 && !texPath.empty())
                {
                    result.texturePaths[slot] = texPath;
                    result.hasTextures[slot] = true;
                    int sampleType = static_cast<int>(TextureSampleType::Color);
                    auto sampleTypeIt = nodeToSampleType.find(nodeId);
                    if (sampleTypeIt != nodeToSampleType.end())
                        sampleType = sampleTypeIt->second;
                    if (nodesWithExplicitSampleType.find(nodeId) == nodesWithExplicitSampleType.end())
                        sampleType = static_cast<int>(GuessTextureSampleTypeFromPath(texPath));
                    result.textureSampleTypes[slot] = sampleType;
                }
            }
        }

        if (result.normalTextureSlot < 0)
        {
            for (int slot = 0; slot < 4; ++slot)
            {
                if (result.hasTextures[slot] &&
                    result.textureSampleTypes[slot] == static_cast<int>(TextureSampleType::Normal))
                {
                    result.normalTextureSlot = slot;
                    break;
                }
            }
        }

        if (result.ormTextureSlot < 0)
        {
            for (int slot = 0; slot < 4; ++slot)
            {
                if (result.hasTextures[slot] &&
                    result.textureSampleTypes[slot] == static_cast<int>(TextureSampleType::Mask))
                {
                    result.ormTextureSlot = slot;
                    break;
                }
            }
        }

        if (result.albedoTextureSlot < 0)
        {
            for (int slot = 0; slot < 4; ++slot)
            {
                if (result.hasTextures[slot] &&
                    result.textureSampleTypes[slot] == static_cast<int>(TextureSampleType::Color))
                {
                    result.albedoTextureSlot = slot;
                    break;
                }
            }
        }

        result.valid = true;
        return result;
    }

    /// Check if a material file exists and is valid
    static bool IsValid(const std::string& path)
    {
        std::ifstream file(path);
        if (!file)
            return false;

        std::string line;
        std::getline(file, line);
        return line.find("DOTMATERIAL") == 0;
    }
};

} // namespace Dot
