// =============================================================================
// Dot Engine - Material Graph Implementation
// =============================================================================

#include "Core/Material/MaterialGraph.h"

#include "Core/Material/MaterialTextureUtils.h"
#include "Core/Material/NodeProperty.h"

#include <array>
#include <algorithm>
#include <fstream>
#include <functional>
#include <queue>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace Dot
{

namespace
{
TextureSampleType ResolveTextureNodeSampleType(const MaterialNode* node)
{
    if (!node || node->GetType() != MaterialNodeType::Texture2D)
        return TextureSampleType::Color;

    const auto* sampleTypeProp = node->GetProperty<TextureSampleTypeProperty>();
    int sampleType = sampleTypeProp ? sampleTypeProp->value : static_cast<int>(TextureSampleType::Color);
    const auto* pathProp = node->GetProperty<TexturePathProperty>();
    if (sampleType == static_cast<int>(TextureSampleType::Color) && pathProp)
        sampleType = static_cast<int>(GuessTextureSampleTypeFromPath(pathProp->path));
    return static_cast<TextureSampleType>(sampleType);
}

int GetPreferredTextureSlot(TextureSampleType sampleType)
{
    switch (sampleType)
    {
        case TextureSampleType::Normal:
            return 1;
        case TextureSampleType::Mask:
            return 2;
        case TextureSampleType::Color:
        default:
            return 0;
    }
}

std::unordered_map<int, int> ResolveEffectiveTextureSlots(const std::vector<std::unique_ptr<MaterialNode>>& nodes)
{
    struct TextureNodeInfo
    {
        int nodeId = -1;
        int requestedSlot = 0;
        TextureSampleType sampleType = TextureSampleType::Color;
        std::string path;
    };

    std::vector<TextureNodeInfo> infos;
    infos.reserve(nodes.size());
    for (const auto& node : nodes)
    {
        if (!node || node->GetType() != MaterialNodeType::Texture2D)
            continue;

        const auto* pathProp = node->GetProperty<TexturePathProperty>();
        if (!pathProp || pathProp->path.empty())
            continue;

        const auto* slotProp = node->GetProperty<TextureSlotProperty>();
        TextureNodeInfo info;
        info.nodeId = node->GetId();
        info.requestedSlot = std::clamp(slotProp ? slotProp->value : 0, 0, 3);
        info.sampleType = ResolveTextureNodeSampleType(node.get());
        info.path = pathProp->path;
        infos.push_back(std::move(info));
    }

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

        const int preferredSlot = GetPreferredTextureSlot(info.sampleType);
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
}
} // namespace

MaterialGraph::MaterialGraph()
{
    // Always create a PBR Output node
    AddNode(MaterialNodeType::PBROutput);
}

MaterialNode* MaterialGraph::AddNode(MaterialNodeType type)
{
    auto node = CreateMaterialNode(type);
    if (!node)
        return nullptr;

    MaterialNode* ptr = node.get();
    m_Nodes.push_back(std::move(node));
    return ptr;
}

void MaterialGraph::RemoveNode(int nodeId)
{
    // Don't allow removing the output node
    if (!m_Nodes.empty() && m_Nodes[0]->GetType() == MaterialNodeType::PBROutput && m_Nodes[0]->GetId() == nodeId)
        return;

    // Remove all connections to/from this node
    auto it = m_Nodes.begin();
    while (it != m_Nodes.end())
    {
        if ((*it)->GetId() == nodeId)
        {
            // Disconnect all pins
            for (auto& pin : (*it)->GetInputs())
                DisconnectPin(pin.id);
            for (auto& pin : (*it)->GetOutputs())
                DisconnectPin(pin.id);

            it = m_Nodes.erase(it);
            break;
        }
        else
        {
            ++it;
        }
    }
}

MaterialNode* MaterialGraph::GetNode(int nodeId)
{
    for (auto& node : m_Nodes)
    {
        if (node->GetId() == nodeId)
            return node.get();
    }
    return nullptr;
}

bool MaterialGraph::Connect(int outputPinId, int inputPinId)
{
    // Validate that we're connecting output to input
    MaterialPin* outPin = GetPinById(outputPinId);
    MaterialPin* inPin = GetPinById(inputPinId);

    if (!outPin || !inPin)
        return false;

    // Check pin directions - must be output to input
    if (outPin->isInput || !inPin->isInput)
    {
        // Try swapping - maybe they passed them in wrong order
        if (!outPin->isInput && inPin->isInput)
        {
            // Already correct, shouldn't reach here
        }
        else if (outPin->isInput && !inPin->isInput)
        {
            // Swapped - fix it
            std::swap(outputPinId, inputPinId);
            std::swap(outPin, inPin);
        }
        else
        {
            // Both same direction - invalid connection
            return false;
        }
    }

    // Disconnect any existing connection to this input
    DisconnectPin(inputPinId);

    // Create new connection
    MaterialConnection conn;
    conn.id = m_NextConnectionId++;
    conn.outputPinId = outputPinId;
    conn.inputPinId = inputPinId;
    m_Connections.push_back(conn);

    // Update pin references
    if (auto* pin = GetPinById(inputPinId))
        pin->linkedPinId = outputPinId;

    return true;
}

void MaterialGraph::Disconnect(int connectionId)
{
    auto it = std::find_if(m_Connections.begin(), m_Connections.end(),
                           [connectionId](const MaterialConnection& c) { return c.id == connectionId; });

    if (it != m_Connections.end())
    {
        // Clear pin reference
        if (auto* pin = GetPinById(it->inputPinId))
            pin->linkedPinId = -1;

        m_Connections.erase(it);
    }
}

void MaterialGraph::DisconnectPin(int pinId)
{
    auto it = m_Connections.begin();
    while (it != m_Connections.end())
    {
        if (it->outputPinId == pinId || it->inputPinId == pinId)
        {
            // Clear pin references
            if (auto* pin = GetPinById(it->inputPinId))
                pin->linkedPinId = -1;

            it = m_Connections.erase(it);
        }
        else
        {
            ++it;
        }
    }
}

int MaterialGraph::GetConnectedOutputPin(int inputPinId) const
{
    for (const auto& conn : m_Connections)
    {
        if (conn.inputPinId == inputPinId)
            return conn.outputPinId;
    }
    return -1;
}

MaterialNode* MaterialGraph::GetNodeByPinId(int pinId)
{
    for (auto& node : m_Nodes)
    {
        for (auto& pin : node->GetInputs())
            if (pin.id == pinId)
                return node.get();
        for (auto& pin : node->GetOutputs())
            if (pin.id == pinId)
                return node.get();
    }
    return nullptr;
}

const MaterialNode* MaterialGraph::GetNodeByPinId(int pinId) const
{
    for (const auto& node : m_Nodes)
    {
        for (const auto& pin : node->GetInputs())
            if (pin.id == pinId)
                return node.get();
        for (const auto& pin : node->GetOutputs())
            if (pin.id == pinId)
                return node.get();
    }
    return nullptr;
}

MaterialPin* MaterialGraph::GetPinById(int pinId)
{
    for (auto& node : m_Nodes)
    {
        for (auto& pin : node->GetInputs())
            if (pin.id == pinId)
                return &pin;
        for (auto& pin : node->GetOutputs())
            if (pin.id == pinId)
                return &pin;
    }
    return nullptr;
}

PBROutputNode* MaterialGraph::GetOutputNode() const
{
    if (!m_Nodes.empty() && m_Nodes[0]->GetType() == MaterialNodeType::PBROutput)
        return static_cast<PBROutputNode*>(m_Nodes[0].get());
    return nullptr;
}

std::vector<MaterialNode*> MaterialGraph::GetSortedNodes() const
{
    // Topological sort based on connections
    std::vector<MaterialNode*> sorted;
    std::unordered_set<int> visited;

    // DFS from output node
    std::function<void(MaterialNode*)> visit = [&](MaterialNode* node)
    {
        if (!node || visited.count(node->GetId()))
            return;

        // Visit dependencies first
        for (const auto& input : node->GetInputs())
        {
            int connectedPinId = GetConnectedOutputPin(input.id);
            if (connectedPinId >= 0)
            {
                // Find node that owns this output pin
                for (const auto& n : m_Nodes)
                {
                    for (const auto& out : n->GetOutputs())
                    {
                        if (out.id == connectedPinId)
                        {
                            visit(n.get());
                            break;
                        }
                    }
                }
            }
        }

        visited.insert(node->GetId());
        sorted.push_back(node);
    };

    // Start from output node
    if (auto* output = GetOutputNode())
        visit(output);

    return sorted;
}

std::string MaterialGraph::GenerateHLSL() const
{
    std::ostringstream ss;
    const std::unordered_map<int, int> effectiveTextureSlots = ResolveEffectiveTextureSlots(m_Nodes);
    std::vector<std::pair<TextureSlotProperty*, int>> overriddenTextureSlots;
    overriddenTextureSlots.reserve(effectiveTextureSlots.size());

    for (const auto& node : m_Nodes)
    {
        if (!node || node->GetType() != MaterialNodeType::Texture2D)
            continue;

        auto slotIt = effectiveTextureSlots.find(node->GetId());
        if (slotIt == effectiveTextureSlots.end())
            continue;

        const auto* slotProp = node->GetProperty<TextureSlotProperty>();
        if (!slotProp)
            continue;

        auto* mutableSlotProp = const_cast<TextureSlotProperty*>(slotProp);
        overriddenTextureSlots.push_back({mutableSlotProp, mutableSlotProp->value});
        mutableSlotProp->value = slotIt->second;
    }

    ss << "// Generated Material Surface Function\n";
    ss << "// =============================================================================\n\n";

    ss << "// --- Noise Helpers ---\n";
    ss << "float2 dot_hash22(float2 p)\n";
    ss << "{\n";
    ss << "    p = float2(dot(p, float2(127.1, 311.7)), dot(p, float2(269.5, 183.3)));\n";
    ss << "    return -1.0 + 2.0 * frac(sin(p) * 43758.5453123);\n";
    ss << "}\n\n";
    ss << "float dot_perlin(float2 p)\n";
    ss << "{\n";
    ss << "    float2 i = floor(p);\n";
    ss << "    float2 f = frac(p);\n";
    ss << "    float2 u = f * f * (3.0 - 2.0 * f);\n";
    ss << "    return lerp(lerp(dot(dot_hash22(i + float2(0.0, 0.0)), f - float2(0.0, 0.0)),\n";
    ss << "                     dot(dot_hash22(i + float2(1.0, 0.0)), f - float2(1.0, 0.0)), u.x),\n";
    ss << "                lerp(dot(dot_hash22(i + float2(0.0, 1.0)), f - float2(0.0, 1.0)),\n";
    ss << "                     dot(dot_hash22(i + float2(1.0, 1.0)), f - float2(1.0, 1.0)), u.x), u.y);\n";
    ss << "}\n\n";
    ss << "float dot_fbm(float2 p, int octaves, float persistence, float lacunarity)\n";
    ss << "{\n";
    ss << "    float total = 0.0;\n";
    ss << "    float amplitude = 1.0;\n";
    ss << "    float frequency = 1.0;\n";
    ss << "    for (int i = 0; i < octaves; i++)\n";
    ss << "    {\n";
    ss << "        total += dot_perlin(p * frequency) * amplitude;\n";
    ss << "        amplitude *= persistence;\n";
    ss << "        frequency *= lacunarity;\n";
    ss << "    }\n";
    ss << "    return total;\n";
    ss << "}\n\n";

    ss << "// --- Material Type Conversion Helpers ---\n";
    ss << "float DotToFloat(float v) { return v; }\n";
    ss << "float DotToFloat(float2 v) { return v.x; }\n";
    ss << "float DotToFloat(float3 v) { return v.x; }\n";
    ss << "float DotToFloat(float4 v) { return v.x; }\n";
    ss << "float2 DotToFloat2(float v) { return float2(v, v); }\n";
    ss << "float2 DotToFloat2(float2 v) { return v; }\n";
    ss << "float2 DotToFloat2(float3 v) { return v.xy; }\n";
    ss << "float2 DotToFloat2(float4 v) { return v.xy; }\n";
    ss << "float3 DotToFloat3(float v) { return float3(v, v, v); }\n";
    ss << "float3 DotToFloat3(float2 v) { return float3(v.x, v.y, 0.0); }\n";
    ss << "float3 DotToFloat3(float3 v) { return v; }\n";
    ss << "float3 DotToFloat3(float4 v) { return v.xyz; }\n";
    ss << "float4 DotToFloat4(float v) { return float4(v, v, v, v); }\n";
    ss << "float4 DotToFloat4(float2 v) { return float4(v.x, v.y, 0.0, 1.0); }\n";
    ss << "float4 DotToFloat4(float3 v) { return float4(v, 1.0); }\n";
    ss << "float4 DotToFloat4(float4 v) { return v; }\n\n";

    // Surface function
    ss << "void GetMaterialSurface(float2 uv, float3 worldPos, float3 worldNormal, float4 worldTangent, inout float3 "
          "albedo, inout float metallic, inout float roughness, inout float ao, inout float3 normal)\n{\n";
    ss << "    // Define helper for UV accessibility in nodes that use it\n";
    ss << "    float2 UV = uv;\n\n";

    // Pin mapping: generate defines for all connections
    ss << "    // Pin mapping\n";
    for (const auto& conn : m_Connections)
    {
        auto* srcNode = GetNodeByPinId(conn.outputPinId);
        if (srcNode)
        {
            int outIdx = srcNode->GetOutputIndex(conn.outputPinId);
            if (outIdx >= 0)
            {
                ss << "    #define pin_" << conn.inputPinId << " " << srcNode->GetVarName(outIdx) << "\n";
            }
        }
    }
    ss << "\n";

    // Generate code for each node in dependency order
    auto sorted = GetSortedNodes();
    for (auto* node : sorted)
    {
        if (node->GetType() == MaterialNodeType::PBROutput)
            continue; // Output node handled separately

        // Node-local pin aliases
        for (const auto& pin : node->GetInputs())
        {
            if (pin.linkedPinId != -1)
            {
                ss << "    #define " << pin.name << " pin_" << pin.id << "\n";
            }
        }

        if (node->GetType() == MaterialNodeType::SplitVec3 || node->GetType() == MaterialNodeType::Time ||
            node->GetType() == MaterialNodeType::Perlin)
        {
            for (int outputIndex = 0; outputIndex < static_cast<int>(node->GetOutputs().size()); ++outputIndex)
            {
                std::string code = node->GenerateHLSL(outputIndex);
                if (!code.empty())
                    ss << "    " << code << "\n";
            }
        }
        else
        {
            // Most nodes emit all needed outputs from GenerateHLSL(0).
            std::string code = node->GenerateHLSL(0);
            if (!code.empty())
                ss << "    " << code << "\n";
        }

        // Cleanup aliases
        for (const auto& pin : node->GetInputs())
        {
            if (pin.linkedPinId != -1)
            {
                ss << "    #undef " << pin.name << "\n";
            }
        }
    }

    // Final output assignment
    ss << "\n    // Output assignment\n";
    if (auto* output = GetOutputNode())
    {
        auto resolveTextureSampleType = [](const MaterialNode* node) -> TextureSampleType
        {
            if (!node || node->GetType() != MaterialNodeType::Texture2D)
                return TextureSampleType::Color;

            const auto* sampleTypeProp = node->GetProperty<TextureSampleTypeProperty>();
            int sampleType = sampleTypeProp ? sampleTypeProp->value : static_cast<int>(TextureSampleType::Color);
            const auto* pathProp = node->GetProperty<TexturePathProperty>();
            if (sampleType == static_cast<int>(TextureSampleType::Color) && pathProp)
                sampleType = static_cast<int>(GuessTextureSampleTypeFromPath(pathProp->path));
            return static_cast<TextureSampleType>(sampleType);
        };

        auto findInputByName = [&](const char* name) -> const MaterialPin*
        {
            for (const auto& pin : output->GetInputs())
            {
                if (pin.name == name)
                    return &pin;
            }
            return nullptr;
        };

        const MaterialPin* albedoPin = findInputByName("Albedo");
        const MaterialPin* metallicPin = findInputByName("Metallic");
        const MaterialPin* roughnessPin = findInputByName("Roughness");
        const MaterialPin* normalPin = findInputByName("Normal");
        const MaterialPin* aoPin = findInputByName("Ambient Occlusion");

        const int albedoConn = albedoPin ? GetConnectedOutputPin(albedoPin->id) : -1;
        const int normalConn = normalPin ? GetConnectedOutputPin(normalPin->id) : -1;
        bool treatAlbedoAsNormalFallback = false;
        if (normalConn < 0 && albedoConn >= 0)
        {
            const MaterialNode* albedoNode = GetNodeByPinId(albedoConn);
            if (resolveTextureSampleType(albedoNode) == TextureSampleType::Normal)
                treatAlbedoAsNormalFallback = true;
        }

        // Albedo
        if (albedoConn >= 0 && !treatAlbedoAsNormalFallback)
        {
            auto* node = GetNodeByPinId(albedoConn);
            if (node)
            {
                int outIdx = node->GetOutputIndex(albedoConn);
                ss << "    albedo = DotToFloat3(" << node->GetVarName(outIdx >= 0 ? outIdx : 0) << ");\n";
            }
        }

        // Metallic
        int metallicConn = metallicPin ? GetConnectedOutputPin(metallicPin->id) : -1;
        if (metallicConn >= 0)
        {
            auto* node = GetNodeByPinId(metallicConn);
            if (node)
            {
                int outIdx = node->GetOutputIndex(metallicConn);
                ss << "    metallic = DotToFloat(" << node->GetVarName(outIdx >= 0 ? outIdx : 0) << ");\n";
            }
        }

        // Roughness
        int roughnessConn = roughnessPin ? GetConnectedOutputPin(roughnessPin->id) : -1;
        if (roughnessConn >= 0)
        {
            auto* node = GetNodeByPinId(roughnessConn);
            if (node)
            {
                int outIdx = node->GetOutputIndex(roughnessConn);
                ss << "    roughness = DotToFloat(" << node->GetVarName(outIdx >= 0 ? outIdx : 0) << ");\n";
            }
        }

        // Ambient Occlusion
        int aoConn = aoPin ? GetConnectedOutputPin(aoPin->id) : -1;
        if (aoConn >= 0)
        {
            auto* node = GetNodeByPinId(aoConn);
            if (node)
            {
                int outIdx = node->GetOutputIndex(aoConn);
                ss << "    ao = saturate(DotToFloat(" << node->GetVarName(outIdx >= 0 ? outIdx : 0) << "));\n";
            }
        }

        // Normal
        if (normalConn >= 0)
        {
            auto* node = GetNodeByPinId(normalConn);
            if (node)
            {
                int outIdx = node->GetOutputIndex(normalConn);
                ss << "    normal = normalize(DotToFloat3(" << node->GetVarName(outIdx >= 0 ? outIdx : 0) << "));\n";
            }
        }
        else if (treatAlbedoAsNormalFallback)
        {
            auto* node = GetNodeByPinId(albedoConn);
            if (node)
            {
                int outIdx = node->GetOutputIndex(albedoConn);
                ss << "    normal = normalize(DotToFloat3(" << node->GetVarName(outIdx >= 0 ? outIdx : 0) << "));\n";
            }
        }
    }

    ss << "}\n";

    for (const auto& [slotProp, originalValue] : overriddenTextureSlots)
        slotProp->value = originalValue;

    return ss.str();
}

bool MaterialGraph::SaveToFile(const std::string& path) const
{
    std::ofstream file(path);
    if (!file)
        return false;

    // Header
    file << "DOTMATERIAL 1.0\n";
    file << "NODES " << m_Nodes.size() << "\n";

    // Save each node
    for (const auto& node : m_Nodes)
    {
        // Format: NODE type id posX posY
        file << "NODE " << static_cast<int>(node->GetType()) << " " << node->GetId() << " " << node->posX << " "
             << node->posY << "\n";

        // Save node-specific data - use property system
        const auto& properties = node->GetProperties();
        file << "PROPERTIES " << properties.size() << "\n";
        for (const auto& prop : properties)
        {
            // Replace spaces with underscores in property name to handle multi-word names
            std::string escapedName = prop->GetName();
            std::replace(escapedName.begin(), escapedName.end(), ' ', '_');
            file << "PROP " << escapedName << " " << prop->Serialize() << "\n";
        }

        // Save pin IDs for reconnection
        file << "INPUTS " << node->GetInputs().size();
        for (const auto& pin : node->GetInputs())
            file << " " << pin.id;
        file << "\n";

        file << "OUTPUTS " << node->GetOutputs().size();
        for (const auto& pin : node->GetOutputs())
            file << " " << pin.id;
        file << "\n";
    }

    // Save connections
    file << "CONNECTIONS " << m_Connections.size() << "\n";
    for (const auto& conn : m_Connections)
    {
        file << "CONN " << conn.id << " " << conn.outputPinId << " " << conn.inputPinId << "\n";
    }

    file << "END\n";
    return true;
}

bool MaterialGraph::LoadFromFile(const std::string& path)
{
    std::ifstream file(path);
    if (!file)
        return false;

    std::string line;

    // Check header
    std::getline(file, line);
    if (line.find("DOTMATERIAL") != 0)
        return false;

    // Clear current graph (except we'll rebuild it)
    m_Nodes.clear();
    m_Connections.clear();
    m_NextConnectionId = 1;

    // Mapping from saved pin IDs to new pin IDs
    std::unordered_map<int, int> pinIdMap;

    // Read node count
    int nodeCount = 0;
    file >> line >> nodeCount;
    std::getline(file, line); // consume newline

    // Load each node
    for (int n = 0; n < nodeCount; ++n)
    {
        std::string token;
        int typeInt, savedId;
        float posX, posY;

        file >> token >> typeInt >> savedId >> posX >> posY;
        std::getline(file, line); // consume newline

        MaterialNodeType type = static_cast<MaterialNodeType>(typeInt);

        // Skip PBROutput since constructor creates one automatically
        // We'll update the existing one's position instead
        MaterialNode* node = nullptr;
        if (type == MaterialNodeType::PBROutput && !m_Nodes.empty())
        {
            node = m_Nodes[0].get();
            node->posX = posX;
            node->posY = posY;
        }
        else if (type != MaterialNodeType::PBROutput)
        {
            node = AddNode(type);
            if (node)
            {
                node->posX = posX;
                node->posY = posY;
            }
        }
        else
        {
            // First node is PBROutput
            auto newNode = CreateMaterialNode(type);
            node = newNode.get();
            node->posX = posX;
            node->posY = posY;
            m_Nodes.push_back(std::move(newNode));
        }

        // Read node-specific data
        std::getline(file, line);
        std::istringstream iss(line);
        iss >> token;

        // Load properties using new property system
        if (token == "PROPERTIES" && node)
        {
            size_t propCount;
            iss >> propCount;

            for (size_t i = 0; i < propCount; ++i)
            {
                std::getline(file, line);
                std::istringstream propIss(line);
                std::string propToken;
                propIss >> propToken;

                if (propToken == "PROP")
                {
                    std::string propNameEscaped;
                    propIss >> propNameEscaped;

                    // Unescape property name (underscores back to spaces)
                    std::string propName = propNameEscaped;
                    std::replace(propName.begin(), propName.end(), '_', ' ');

                    std::string propData;
                    std::getline(propIss, propData);
                    // Trim leading whitespace
                    size_t start = propData.find_first_not_of(" \t");
                    if (start != std::string::npos)
                        propData = propData.substr(start);

                    // Find matching property by name and deserialize
                    for (const auto& prop : node->GetProperties())
                    {
                        if (prop->GetName() == propName)
                        {
                            prop->Deserialize(propData);
                            break;
                        }
                    }
                }
            }
        }
        // Backward compatibility: support old VALUE/PATH/SLOT format
        else if (token == "VALUE" && node)
        {
            if (type == MaterialNodeType::ConstFloat)
            {
                float val;
                iss >> val;
                auto* prop = node->GetProperty<FloatProperty>();
                if (prop)
                    prop->value = val;
            }
            else if (type == MaterialNodeType::ConstVec3)
            {
                float x, y, z;
                iss >> x >> y >> z;
                auto* prop = node->GetProperty<Vec3Property>();
                if (prop)
                    prop->value = Vec3(x, y, z);
            }
        }
        else if (token == "PATH" && type == MaterialNodeType::Texture2D && node)
        {
            std::string texPath;
            std::getline(iss, texPath);
            size_t start = texPath.find_first_not_of(" \t");
            if (start != std::string::npos)
                texPath = texPath.substr(start);
            auto* pathProp = node->GetProperty<FilePathProperty>();
            if (pathProp)
                pathProp->path = texPath;

            // Read slot (old format)
            std::getline(file, line);
            std::istringstream slotIss(line);
            slotIss >> token;
            int slot;
            slotIss >> slot;
            auto* slotProp = node->GetProperty<TextureSlotProperty>();
            if (slotProp)
                slotProp->value = slot;
        }

        // Read input pin IDs
        if (token != "INPUTS")
        {
            std::getline(file, line);
            std::istringstream tempIss(line);
            tempIss >> token;
        }

        if (token == "INPUTS" || line.find("INPUTS") == 0)
        {
            std::istringstream inputIss(line);
            std::string inputToken;
            inputIss >> inputToken; // Skip "INPUTS" token

            int inputCount = 0;
            inputIss >> inputCount;

            if (node)
            {
                auto& inputs = node->GetInputs();
                for (int i = 0; i < inputCount && i < static_cast<int>(inputs.size()); ++i)
                {
                    int savedPinId;
                    inputIss >> savedPinId;
                    pinIdMap[savedPinId] = inputs[i].id;
                }
            }
        }

        // Read output pin IDs
        std::getline(file, line);
        std::istringstream outputIss(line);
        outputIss >> token;
        if (token == "OUTPUTS" && node)
        {
            int outputCount;
            outputIss >> outputCount;

            auto& outputs = node->GetOutputs();
            for (int i = 0; i < outputCount && i < static_cast<int>(outputs.size()); ++i)
            {
                int savedPinId;
                outputIss >> savedPinId;
                pinIdMap[savedPinId] = outputs[i].id;
            }
        }
    }

    // Read connections
    int connCount = 0;
    file >> line >> connCount;
    std::getline(file, line); // consume newline

    for (int c = 0; c < connCount; ++c)
    {
        std::string token;
        int connId, savedOutputPin, savedInputPin;
        file >> token >> connId >> savedOutputPin >> savedInputPin;
        std::printf("[LoadFromFile] Connection %d: token='%s' out=%d in=%d\n", c, token.c_str(), savedOutputPin,
                    savedInputPin);

        // Map old pin IDs to new ones
        auto outIt = pinIdMap.find(savedOutputPin);
        auto inIt = pinIdMap.find(savedInputPin);

        if (outIt != pinIdMap.end() && inIt != pinIdMap.end())
        {
            Connect(outIt->second, inIt->second);
        }
    }

    return true;
}

} // namespace Dot
