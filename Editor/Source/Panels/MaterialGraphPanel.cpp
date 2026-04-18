// =============================================================================
// Dot Engine - Material Graph Editor Panel Implementation
// =============================================================================
// Custom node editor using ImGui's drawing API
// =============================================================================

#include "MaterialGraphPanel.h"
#include "PanelChrome.h"

#include "Core/Assets/AssetManager.h"
#include "Core/Material/MaterialTextureUtils.h"

#include "../../../ThirdParty/stb/stb_image.h"
#include "../Rendering/MaterialPreviewRenderer.h"
#include "RHI/RHIDevice.h"
#include "RHI/RHIGUI.h"
#include "RHI/RHITexture.h"
#include "RHI/RHITypes.h"
#include "Utils/FileDialogs.h"

#include <Core/Material/NodeProperty.h>
#include <Core/Math/Vec2.h>
#include <Core/Math/Vec4.h> // Ensure Vec4 is also covered
#include <array>
#include <algorithm>        // For std::clamp, std::sort
#include <cmath>            // For sin, cos, pow
#include <filesystem>       // For std::filesystem
#include <functional>       // For std::greater
#include <imgui.h>
#include <imgui_internal.h> // For ImDrawList bezier curves


namespace Dot
{

// --- Noise Helpers for Preview ---
static float dot_lerp(float a, float b, float t)
{
    return a + t * (b - a);
}

static float dot_dot(const Vec2& a, const Vec2& b)
{
    return a.x * b.x + a.y * b.y;
}

static Vec2 dot_hash22_grad(Vec2 p)
{
    float x = p.x * 127.1f + p.y * 311.7f;
    float y = p.x * 269.5f + p.y * 183.3f;
    float rx = sinf(x) * 43758.5453123f;
    float ry = sinf(y) * 43758.5453123f;
    return Vec2((rx - floorf(rx)) * 2.0f - 1.0f, (ry - floorf(ry)) * 2.0f - 1.0f);
}

static float dot_perlin_preview(Vec2 p)
{
    Vec2 i(floorf(p.x), floorf(p.y));
    Vec2 f(p.x - i.x, p.y - i.y);
    Vec2 u(f.x * f.x * (3.0f - 2.0f * f.x), f.y * f.y * (3.0f - 2.0f * f.y));

    float a = dot_dot(dot_hash22_grad(i), f);
    float b = dot_dot(dot_hash22_grad(i + Vec2(1, 0)), f - Vec2(1, 0));
    float c = dot_dot(dot_hash22_grad(i + Vec2(0, 1)), f - Vec2(0, 1));
    float d = dot_dot(dot_hash22_grad(i + Vec2(1, 1)), f - Vec2(1, 1));

    return dot_lerp(dot_lerp(a, b, u.x), dot_lerp(c, d, u.x), u.y);
}

static float dot_fbm_preview(Vec2 p, int octaves, float persistence, float lacunarity)
{
    float total = 0.0f;
    float amplitude = 1.0f;
    float frequency = 1.0f;
    for (int i = 0; i < octaves; i++)
    {
        total += dot_perlin_preview(Vec2(p.x * frequency, p.y * frequency)) * amplitude;
        amplitude *= persistence;
        frequency *= lacunarity;
    }
    return total;
}

static constexpr int kThumbnailMaxDimension = 128;
static constexpr int kThumbnailLoadsPerFrame = 1;

static std::string NormalizeTextureCacheKey(const std::string& path)
{
    if (path.empty())
        return {};

    std::error_code ec;
    std::filesystem::path normalized = std::filesystem::weakly_canonical(std::filesystem::path(path), ec);
    if (ec)
        normalized = std::filesystem::path(path).lexically_normal();

    return normalized.generic_string();
}

static void DownsampleThumbnailRGBA(const unsigned char* source, int srcWidth, int srcHeight, int dstWidth, int dstHeight,
                                    std::vector<unsigned char>& outData)
{
    outData.resize(static_cast<size_t>(dstWidth) * static_cast<size_t>(dstHeight) * 4);
    if (!source || srcWidth <= 0 || srcHeight <= 0 || dstWidth <= 0 || dstHeight <= 0)
        return;

    for (int y = 0; y < dstHeight; ++y)
    {
        const int srcY = std::clamp((y * srcHeight) / dstHeight, 0, srcHeight - 1);
        for (int x = 0; x < dstWidth; ++x)
        {
            const int srcX = std::clamp((x * srcWidth) / dstWidth, 0, srcWidth - 1);
            const size_t srcIndex = (static_cast<size_t>(srcY) * static_cast<size_t>(srcWidth) + static_cast<size_t>(srcX)) * 4;
            const size_t dstIndex = (static_cast<size_t>(y) * static_cast<size_t>(dstWidth) + static_cast<size_t>(x)) * 4;
            outData[dstIndex + 0] = source[srcIndex + 0];
            outData[dstIndex + 1] = source[srcIndex + 1];
            outData[dstIndex + 2] = source[srcIndex + 2];
            outData[dstIndex + 3] = source[srcIndex + 3];
        }
    }
}

static TextureSampleType ResolveTextureNodeSampleType(const MaterialNode* node)
{
    if (!node || node->GetType() != MaterialNodeType::Texture2D)
        return TextureSampleType::Color;

    const auto* sampleTypeProp = node->GetProperty<TextureSampleTypeProperty>();
    int sampleType = sampleTypeProp ? sampleTypeProp->value : static_cast<int>(TextureSampleType::Color);
    const auto* pathProp = node->GetProperty<TexturePathProperty>();
    if (sampleType == static_cast<int>(TextureSampleType::Color) && pathProp)
    {
        sampleType = static_cast<int>(GuessTextureSampleTypeFromPath(pathProp->path));
    }
    return static_cast<TextureSampleType>(sampleType);
}

static int GetPreferredTextureSlot(TextureSampleType sampleType)
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

static std::unordered_map<int, int> ResolveEffectiveTextureSlots(const MaterialGraph* graph)
{
    std::unordered_map<int, int> resolvedSlots;
    if (!graph)
        return resolvedSlots;

    struct TextureNodeInfo
    {
        int nodeId = -1;
        int requestedSlot = 0;
        TextureSampleType sampleType = TextureSampleType::Color;
        std::string path;
    };

    std::vector<TextureNodeInfo> infos;
    infos.reserve(graph->GetNodes().size());
    for (const auto& node : graph->GetNodes())
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
        info.path = NormalizeTextureCacheKey(pathProp->path);
        infos.push_back(std::move(info));
    }

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

static void AutoRouteTextureNodeToPbrInput(MaterialGraph* graph, MaterialNode* node)
{
    if (!graph || !node || node->GetType() != MaterialNodeType::Texture2D)
        return;

    auto* sampleTypeProp = node->GetProperty<TextureSampleTypeProperty>();
    auto* pathProp = node->GetProperty<TexturePathProperty>();
    if (!sampleTypeProp)
        return;

    int sampleType = sampleTypeProp->value;
    if (sampleType == static_cast<int>(TextureSampleType::Color) && pathProp)
    {
        sampleType = static_cast<int>(GuessTextureSampleTypeFromPath(pathProp->path));
        sampleTypeProp->value = sampleType;
    }

    PBROutputNode* outputNode = graph->GetOutputNode();
    if (!outputNode)
        return;

    int rgbOutputPinId = -1;
    int rOutputPinId = -1;
    int gOutputPinId = -1;
    int bOutputPinId = -1;
    for (const auto& pin : node->GetOutputs())
    {
        if (pin.name == "RGB")
            rgbOutputPinId = pin.id;
        else if (pin.name == "R")
            rOutputPinId = pin.id;
        else if (pin.name == "G")
            gOutputPinId = pin.id;
        else if (pin.name == "B")
            bOutputPinId = pin.id;
    }

    MaterialPin* albedoPin = nullptr;
    MaterialPin* normalPin = nullptr;
    MaterialPin* metallicPin = nullptr;
    MaterialPin* roughnessPin = nullptr;
    MaterialPin* aoPin = nullptr;
    for (auto& pin : outputNode->GetInputs())
    {
        if (pin.name == "Albedo")
            albedoPin = &pin;
        else if (pin.name == "Normal")
            normalPin = &pin;
        else if (pin.name == "Metallic")
            metallicPin = &pin;
        else if (pin.name == "Roughness")
            roughnessPin = &pin;
        else if (pin.name == "Ambient Occlusion")
            aoPin = &pin;
    }

    if (sampleType == static_cast<int>(TextureSampleType::Normal))
    {
        if (!albedoPin || !normalPin || normalPin->linkedPinId != -1 || rgbOutputPinId < 0)
            return;

        int albedoConnectionId = -1;
        for (const auto& conn : graph->GetConnections())
        {
            if (conn.outputPinId == rgbOutputPinId && conn.inputPinId == albedoPin->id)
            {
                albedoConnectionId = conn.id;
                break;
            }
        }

        if (albedoConnectionId >= 0)
        {
            graph->Disconnect(albedoConnectionId);
            graph->Connect(rgbOutputPinId, normalPin->id);
        }
        return;
    }

    if (sampleType != static_cast<int>(TextureSampleType::Mask))
        return;

    if (aoPin && aoPin->linkedPinId == -1 && rOutputPinId >= 0)
        graph->Connect(rOutputPinId, aoPin->id);
    if (roughnessPin && roughnessPin->linkedPinId == -1 && gOutputPinId >= 0)
        graph->Connect(gOutputPinId, roughnessPin->id);
    if (metallicPin && metallicPin->linkedPinId == -1 && bOutputPinId >= 0)
        graph->Connect(bOutputPinId, metallicPin->id);
}

// =============================================================================
// Color constants for the node editor
// =============================================================================
namespace NodeColors
{
const ImU32 Grid = IM_COL32(40, 40, 45, 255);
const ImU32 GridLine = IM_COL32(60, 60, 65, 255);
const ImU32 NodeBg = IM_COL32(50, 50, 55, 230);
const ImU32 NodeBgHovered = IM_COL32(60, 60, 65, 230);
const ImU32 NodeBgSelected = IM_COL32(70, 70, 80, 230);
const ImU32 NodeBorder = IM_COL32(100, 100, 110, 255);
const ImU32 NodeBorderSelected = IM_COL32(120, 150, 200, 255);
const ImU32 TitleBar = IM_COL32(80, 100, 140, 255);
const ImU32 TitleText = IM_COL32(220, 220, 220, 255);
const ImU32 Connection = IM_COL32(150, 180, 220, 200);
const ImU32 ConnectionHovered = IM_COL32(200, 220, 255, 255);

// Pin colors by type
const ImU32 PinFloat = IM_COL32(150, 150, 150, 255);
const ImU32 PinVec2 = IM_COL32(100, 200, 100, 255);
const ImU32 PinVec3 = IM_COL32(230, 200, 80, 255);
const ImU32 PinVec4 = IM_COL32(200, 100, 200, 255);
const ImU32 PinTexture = IM_COL32(100, 150, 230, 255);

// Category colors (Unreal-style)
const ImU32 CatConstant = IM_COL32(85, 120, 85, 255);    // Greenish
const ImU32 CatMath = IM_COL32(85, 100, 120, 255);       // Blue-gray
const ImU32 CatTexture = IM_COL32(160, 140, 80, 255);    // Yellow/Orange
const ImU32 CatUtility = IM_COL32(120, 85, 120, 255);    // Purple
const ImU32 CatProcedural = IM_COL32(80, 160, 140, 255); // Tealish/Green
const ImU32 CatOutput = IM_COL32(140, 70, 70, 255);      // Red
} // namespace NodeColors

// Node dimensions
constexpr float NODE_WIDTH = 180.0f;
constexpr float NODE_TITLE_HEIGHT = 26.0f;
constexpr float NODE_PIN_RADIUS = 6.0f;
constexpr float NODE_PIN_SPACING = 22.0f;
constexpr float NODE_PADDING = 8.0f;
constexpr float GRID_SIZE = 32.0f;

// =============================================================================
// Helper functions
// =============================================================================

static ImU32 GetPinColor(MaterialPinType type)
{
    switch (type)
    {
        case MaterialPinType::Float:
            return NodeColors::PinFloat;
        case MaterialPinType::Vec2:
            return NodeColors::PinVec2;
        case MaterialPinType::Vec3:
            return NodeColors::PinVec3;
        case MaterialPinType::Vec4:
            return NodeColors::PinVec4;
        case MaterialPinType::Texture:
            return NodeColors::PinTexture;
        default:
            return NodeColors::PinFloat;
    }
}

static float GetNodeHeight(MaterialNode* node)
{
    int inputCount = static_cast<int>(node->GetInputs().size());
    int outputCount = static_cast<int>(node->GetOutputs().size());
    int maxPins = std::max(inputCount, outputCount);
    float h = NODE_TITLE_HEIGHT + NODE_PIN_SPACING * (maxPins + 1) + NODE_PADDING;

    // Add extra height for previews
    switch (node->GetType())
    {
        case MaterialNodeType::ConstVec3:
        case MaterialNodeType::ConstVec4:
            h += 30.0f; // Color swatch height
            break;
        case MaterialNodeType::Texture2D:
            h += 70.0f; // Thumbnail height
            break;
        case MaterialNodeType::ConstFloat:
            h += 20.0f; // Numeric display
            break;
        default:
            break;
    }
    return h;
}

// =============================================================================
// MaterialGraphPanel
// =============================================================================

MaterialGraphPanel::MaterialGraphPanel() : EditorPanel("Material Graph")
{
    NewMaterial();
}

MaterialGraphPanel::~MaterialGraphPanel()
{
    ClearTextureCache();
}

void MaterialGraphPanel::ClearTextureCache()
{
    // Clear CPU cache
    for (auto& [path, entry] : m_TextureCache)
    {
        if (entry.data)
        {
            stbi_image_free(entry.data);
            entry.data = nullptr;
        }
    }
    m_TextureCache.clear();

    // Clear GPU cache
    for (auto& [path, thumb] : m_GPUThumbnails)
    {
        if (thumb.imGuiTexId && m_GUI)
        {
            m_GUI->UnregisterTexture(thumb.imGuiTexId);
        }
        thumb.resource.reset();
    }
    m_GPUThumbnails.clear();
    m_LastPreviewTextureSignature.clear();
}

void* MaterialGraphPanel::GetGPUThumbnail(const std::string& path)
{
    if (path.empty() || !m_Device || !m_GUI)
        return nullptr;

    const std::string cacheKey = NormalizeTextureCacheKey(path);
    if (cacheKey.empty())
        return nullptr;

    // Check cache
    auto it = m_GPUThumbnails.find(cacheKey);
    if (it != m_GPUThumbnails.end())
        return it->second.imGuiTexId;

    if (m_ThumbnailLoadsThisFrame >= kThumbnailLoadsPerFrame)
        return nullptr;
    ++m_ThumbnailLoadsThisFrame;

    // Load with stb_image
    int width, height, channels;
    stbi_set_flip_vertically_on_load(false);
    unsigned char* data = stbi_load(cacheKey.c_str(), &width, &height, &channels, 4);
    if (!data)
        return nullptr;

    int thumbnailWidth = width;
    int thumbnailHeight = height;
    if (thumbnailWidth > kThumbnailMaxDimension || thumbnailHeight > kThumbnailMaxDimension)
    {
        if (thumbnailWidth >= thumbnailHeight)
        {
            thumbnailHeight = std::max(1, (thumbnailHeight * kThumbnailMaxDimension) / std::max(1, thumbnailWidth));
            thumbnailWidth = kThumbnailMaxDimension;
        }
        else
        {
            thumbnailWidth = std::max(1, (thumbnailWidth * kThumbnailMaxDimension) / std::max(1, thumbnailHeight));
            thumbnailHeight = kThumbnailMaxDimension;
        }
    }

    std::vector<unsigned char> thumbnailPixels;
    const unsigned char* uploadData = data;
    if (thumbnailWidth != width || thumbnailHeight != height)
    {
        DownsampleThumbnailRGBA(data, width, height, thumbnailWidth, thumbnailHeight, thumbnailPixels);
        uploadData = thumbnailPixels.data();
    }

    // Create RHI texture
    RHITextureDesc desc;
    desc.width = static_cast<uint32_t>(thumbnailWidth);
    desc.height = static_cast<uint32_t>(thumbnailHeight);
    desc.format = RHIFormat::R8G8B8A8_UNORM;
    desc.usage = RHITextureUsage::Sampled;
    desc.debugName = "ThumbnailTexture";

    RHITexturePtr texture = m_Device->CreateTexture(desc);
    if (!texture)
    {
        stbi_image_free(data);
        return nullptr;
    }

    // Upload data
    texture->Update(uploadData);
    stbi_image_free(data);

    // Register with ImGui
    void* nativeTex = m_Device->GetNativeTextureResource(texture.get());
    void* imGuiTexId = m_GUI->RegisterTexture(nativeTex);

    // Cache and return
    GPUThumbnail thumb;
    thumb.resource = texture;
    thumb.imGuiTexId = imGuiTexId;
    m_GPUThumbnails[cacheKey] = thumb;

    return imGuiTexId;
}

void MaterialGraphPanel::NewMaterial()
{
    m_Graph = std::make_unique<MaterialGraph>();
    m_SelectedNodeId = -1;
    m_ScrollOffset = ImVec2(0, 0);
    m_DraggingNode = -1;
    m_CreatingConnection = false;
    m_ConnectionStartPin = -1;
    m_HoveredPin = -1;
    m_LastGeneratedHLSL.clear();
    m_LastPreviewTextureSignature.clear();
}

void MaterialGraphPanel::LoadMaterial(const std::string& path)
{
    m_Graph = std::make_unique<MaterialGraph>();
    m_Graph->LoadFromFile(path);
    m_LastGeneratedHLSL.clear();
    m_LastPreviewTextureSignature.clear();
}

void MaterialGraphPanel::SaveMaterial(const std::string& path)
{
    if (m_Graph)
        m_Graph->SaveToFile(path);
}

std::string MaterialGraphPanel::GetGeneratedCode() const
{
    if (m_Graph)
        return m_Graph->GenerateHLSL();
    return "";
}

bool MaterialGraphPanel::InitializePreview()
{
    if (!m_Device || !m_GUI)
        return false;

    if (!m_PreviewRenderer)
    {
        m_PreviewRenderer = std::make_unique<MaterialPreviewRenderer>();
        if (m_PreviewRenderer->Initialize(m_Device, m_GUI, 256))
        {
            m_UseGPUPreview = true;
            return true;
        }
        else
        {
            m_PreviewRenderer.reset();
            return false;
        }
    }
    return m_PreviewRenderer->IsInitialized();
}

void MaterialGraphPanel::OnImGui()
{
    m_ThumbnailLoadsThisFrame = 0;

    // Handle keyboard shortcuts (Ctrl+Z, Ctrl+Y, Ctrl+C, Ctrl+V, etc.)
    HandleKeyboardShortcuts();

    DrawToolbar();
    ImGui::Separator();

    // Main content area
    ImVec2 contentSize = ImGui::GetContentRegionAvail();
    float paletteWidth = 180.0f;
    float propertyWidth = 220.0f;
    float canvasWidth = contentSize.x - paletteWidth - propertyWidth - 16.0f;

    // Left: Node Palette
    ImGui::BeginChild("NodePalette", ImVec2(paletteWidth, contentSize.y), true);
    DrawNodePalette();
    ImGui::EndChild();

    ImGui::SameLine();

    // Center: Node Canvas
    ImGui::BeginChild("NodeCanvas", ImVec2(canvasWidth, contentSize.y), true,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoMove);
    DrawNodeCanvas();
    ImGui::EndChild();

    ImGui::SameLine();

    // Right: Properties
    ImGui::BeginChild("Properties", ImVec2(propertyWidth, contentSize.y), true);
    DrawPropertyPanel();
    ImGui::EndChild();

    if (m_ShowCodePreview)
        DrawCodePreview();

    // Material preview window (separate floating window)
    DrawPreviewPanel();
}

void MaterialGraphPanel::DrawToolbar()
{
    if (ImGui::Button("New"))
        NewMaterial();

    ImGui::SameLine();
    if (ImGui::Button("Save"))
    {
        FileFilter filter{"Dot Material", "*.dotmat"};
        std::string filepath = FileDialogs::SaveFile(filter, "material.dotmat");
        if (!filepath.empty() && m_Graph)
        {
            m_Graph->SaveToFile(filepath);
            std::printf("Material saved to %s\n", filepath.c_str());
        }
    }

    ImGui::SameLine();
    if (ImGui::Button("Save to Assets"))
    {
        std::filesystem::path assetsPath = std::filesystem::path(AssetManager::Get().GetRootPath()) / "Materials";
        if (!std::filesystem::exists(assetsPath))
        {
            std::filesystem::create_directories(assetsPath);
        }

        // Save with a unique name
        static char materialName[64] = "NewMaterial";
        ImGui::OpenPopup("SaveMaterialPopup");
    }

    // Save to Assets popup
    if (ImGui::BeginPopup("SaveMaterialPopup"))
    {
        static char materialName[64] = "NewMaterial";
        ImGui::Text("Material Name:");
        ImGui::InputText("##matname", materialName, sizeof(materialName));

        if (ImGui::Button("Save"))
        {
            std::filesystem::path assetsPath = std::filesystem::path(AssetManager::Get().GetRootPath()) / "Materials";
            if (!std::filesystem::exists(assetsPath))
            {
                std::filesystem::create_directories(assetsPath);
            }

            std::string filename = std::string(materialName) + ".dotmat";
            std::filesystem::path filepath = assetsPath / filename;

            if (m_Graph && m_Graph->SaveToFile(filepath.string()))
            {
                std::printf("Material saved to Assets/Materials/%s\n", filename.c_str());
            }
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel"))
        {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    ImGui::SameLine();
    if (ImGui::Button("Load"))
    {
        FileFilter filter{"Dot Material", "*.dotmat"};
        std::string filepath = FileDialogs::OpenFile(filter);
        if (!filepath.empty() && m_Graph)
        {
            if (m_Graph->LoadFromFile(filepath))
            {
                std::printf("Material loaded from %s\n", filepath.c_str());
            }
            else
            {
                std::printf("Failed to load material from %s\n", filepath.c_str());
            }
        }
    }

    ImGui::SameLine();
    if (ImGui::Button("Compile"))
        m_ShowCodePreview = true;

    ImGui::SameLine();
    ImGui::Checkbox("Show HLSL", &m_ShowCodePreview);

    ImGui::SameLine();
    ImGui::Separator();
    ImGui::SameLine();

    // Tool buttons
    DrawToolButtons();

    ImGui::SameLine();
    ImGui::Separator();
    ImGui::SameLine();

    // Undo/Redo buttons
    if (ImGui::Button("Undo"))
        Undo();
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Undo (Ctrl+Z)");

    ImGui::SameLine();
    if (ImGui::Button("Redo"))
        Redo();
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Redo (Ctrl+Y)");

    ImGui::SameLine();
    ImGui::Separator();
    ImGui::SameLine();

    // Snap to Grid toggle
    if (m_SnapToGrid)
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.6f, 0.2f, 1.0f));
    if (ImGui::Button(m_SnapToGrid ? "Snap: ON" : "Snap: OFF"))
        m_SnapToGrid = !m_SnapToGrid;
    if (m_SnapToGrid)
        ImGui::PopStyleColor();
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Toggle Snap to Grid (G key)");

    ImGui::SameLine();
    if (ImGui::Button("Align"))
        SnapSelectedNodes();
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Snap selected nodes to grid (Ctrl+G)");

    ImGui::SameLine();
    ImGui::Separator();
    ImGui::SameLine();

    // Status text based on current tool
    switch (m_CurrentTool)
    {
        case GraphTool::Select:
            ImGui::TextDisabled("Ctrl+C/V Copy/Paste | Del to delete");
            break;
        case GraphTool::Knife:
            ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "Knife: Drag across wires to cut");
            break;
        case GraphTool::BoxSelect:
            ImGui::TextColored(ImVec4(0.3f, 0.7f, 1.0f, 1.0f), "Box Select: Drag to select nodes");
            break;
    }
}

void MaterialGraphPanel::DrawToolButtons()
{
    // Select tool
    bool isSelect = (m_CurrentTool == GraphTool::Select);
    if (isSelect)
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.3f, 0.5f, 0.8f, 1.0f));
    if (ImGui::Button("Select [V]") || (!ImGui::GetIO().WantCaptureKeyboard && ImGui::IsKeyPressed(ImGuiKey_V)))
        m_CurrentTool = GraphTool::Select;
    if (isSelect)
        ImGui::PopStyleColor();

    ImGui::SameLine();

    // Knife tool
    bool isKnife = (m_CurrentTool == GraphTool::Knife);
    if (isKnife)
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.4f, 0.1f, 1.0f));
    if (ImGui::Button("Knife [K]") || (!ImGui::GetIO().WantCaptureKeyboard && ImGui::IsKeyPressed(ImGuiKey_K)))
        m_CurrentTool = GraphTool::Knife;
    if (isKnife)
        ImGui::PopStyleColor();

    ImGui::SameLine();

    // Box select tool
    bool isBox = (m_CurrentTool == GraphTool::BoxSelect);
    if (isBox)
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.6f, 0.9f, 1.0f));
    if (ImGui::Button("Box [B]") || (!ImGui::GetIO().WantCaptureKeyboard && ImGui::IsKeyPressed(ImGuiKey_B)))
        m_CurrentTool = GraphTool::BoxSelect;
    if (isBox)
        ImGui::PopStyleColor();
}

void MaterialGraphPanel::DrawNodePalette()
{
    ImGui::Text("Add Nodes");
    ImGui::Separator();

    if (ImGui::CollapsingHeader("Constants", ImGuiTreeNodeFlags_DefaultOpen))
    {
        if (ImGui::Button("Float", ImVec2(-1, 0)))
        {
            auto* node = m_Graph->AddNode(MaterialNodeType::ConstFloat);
            if (node)
            {
                node->posX = 100 - m_ScrollOffset.x;
                node->posY = 100 - m_ScrollOffset.y;
            }
        }
        if (ImGui::Button("Color (Vec3)", ImVec2(-1, 0)))
        {
            auto* node = m_Graph->AddNode(MaterialNodeType::ConstVec3);
            if (node)
            {
                node->posX = 100 - m_ScrollOffset.x;
                node->posY = 150 - m_ScrollOffset.y;
            }
        }
    }

    if (ImGui::CollapsingHeader("Textures", ImGuiTreeNodeFlags_DefaultOpen))
    {
        if (ImGui::Button("Texture 2D", ImVec2(-1, 0)))
        {
            auto* node = m_Graph->AddNode(MaterialNodeType::Texture2D);
            if (node)
            {
                node->posX = 100 - m_ScrollOffset.x;
                node->posY = 200 - m_ScrollOffset.y;
            }
        }
        if (ImGui::Button("Texture Coord", ImVec2(-1, 0)))
        {
            auto* node = m_Graph->AddNode(MaterialNodeType::TextureCoord);
            if (node)
            {
                node->posX = 50 - m_ScrollOffset.x;
                node->posY = 200 - m_ScrollOffset.y;
            }
        }
    }

    if (ImGui::CollapsingHeader("Math - Basic", ImGuiTreeNodeFlags_DefaultOpen))
    {
        if (ImGui::Button("Add (+)", ImVec2(-1, 0)))
            m_Graph->AddNode(MaterialNodeType::Add);
        if (ImGui::Button("Subtract (-)", ImVec2(-1, 0)))
            m_Graph->AddNode(MaterialNodeType::Subtract);
        if (ImGui::Button("Multiply (*)", ImVec2(-1, 0)))
            m_Graph->AddNode(MaterialNodeType::Multiply);
        if (ImGui::Button("Divide (/)", ImVec2(-1, 0)))
            m_Graph->AddNode(MaterialNodeType::Divide);
        if (ImGui::Button("Power (^)", ImVec2(-1, 0)))
            m_Graph->AddNode(MaterialNodeType::Power);
    }

    if (ImGui::CollapsingHeader("Math - Functions"))
    {
        if (ImGui::Button("Abs |x|", ImVec2(-1, 0)))
            m_Graph->AddNode(MaterialNodeType::Abs);
        if (ImGui::Button("Negate (-x)", ImVec2(-1, 0)))
            m_Graph->AddNode(MaterialNodeType::Negate);
        if (ImGui::Button("One Minus (1-x)", ImVec2(-1, 0)))
            m_Graph->AddNode(MaterialNodeType::OneMinus);
        if (ImGui::Button("Saturate [0,1]", ImVec2(-1, 0)))
            m_Graph->AddNode(MaterialNodeType::Saturate);
        if (ImGui::Button("Sqrt", ImVec2(-1, 0)))
            m_Graph->AddNode(MaterialNodeType::Sqrt);
    }

    if (ImGui::CollapsingHeader("Math - Trig"))
    {
        if (ImGui::Button("Sin", ImVec2(-1, 0)))
            m_Graph->AddNode(MaterialNodeType::Sin);
        if (ImGui::Button("Cos", ImVec2(-1, 0)))
            m_Graph->AddNode(MaterialNodeType::Cos);
    }

    if (ImGui::CollapsingHeader("Math - Rounding"))
    {
        if (ImGui::Button("Floor", ImVec2(-1, 0)))
            m_Graph->AddNode(MaterialNodeType::Floor);
        if (ImGui::Button("Ceil", ImVec2(-1, 0)))
            m_Graph->AddNode(MaterialNodeType::Ceil);
        if (ImGui::Button("Frac", ImVec2(-1, 0)))
            m_Graph->AddNode(MaterialNodeType::Frac);
    }

    if (ImGui::CollapsingHeader("Math - Range"))
    {
        if (ImGui::Button("Min", ImVec2(-1, 0)))
            m_Graph->AddNode(MaterialNodeType::Min);
        if (ImGui::Button("Max", ImVec2(-1, 0)))
            m_Graph->AddNode(MaterialNodeType::Max);
        if (ImGui::Button("Clamp", ImVec2(-1, 0)))
            m_Graph->AddNode(MaterialNodeType::Clamp);
        if (ImGui::Button("Lerp", ImVec2(-1, 0)))
            m_Graph->AddNode(MaterialNodeType::Lerp);
        if (ImGui::Button("Smoothstep", ImVec2(-1, 0)))
            m_Graph->AddNode(MaterialNodeType::Smoothstep);
    }

    if (ImGui::CollapsingHeader("Vector"))
    {
        if (ImGui::Button("Split RGB", ImVec2(-1, 0)))
            m_Graph->AddNode(MaterialNodeType::SplitVec3);
        if (ImGui::Button("Make RGB", ImVec2(-1, 0)))
            m_Graph->AddNode(MaterialNodeType::MakeVec3);
        if (ImGui::Button("Normalize", ImVec2(-1, 0)))
            m_Graph->AddNode(MaterialNodeType::Normalize);
        if (ImGui::Button("Dot Product", ImVec2(-1, 0)))
            m_Graph->AddNode(MaterialNodeType::Dot);
    }

    if (ImGui::CollapsingHeader("Utility"))
    {
        if (ImGui::Button("Time", ImVec2(-1, 0)))
            m_Graph->AddNode(MaterialNodeType::Time);
        if (ImGui::Button("Fresnel", ImVec2(-1, 0)))
            m_Graph->AddNode(MaterialNodeType::Fresnel);
        if (ImGui::Button("Panner", ImVec2(-1, 0)))
            m_Graph->AddNode(MaterialNodeType::Panner);
    }
}

void MaterialGraphPanel::DrawNodeCanvas()
{
    if (!m_Graph)
        return;

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    ImVec2 canvasPos = ImGui::GetCursorScreenPos();
    ImVec2 canvasSize = ImGui::GetContentRegionAvail();

    // Create canvas area - captures all mouse buttons
    ImGui::InvisibleButton("canvas", canvasSize,
                           ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight |
                               ImGuiButtonFlags_MouseButtonMiddle);

    if (ImGui::BeginDragDropTarget())
    {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("TEXTURE_ASSET"))
        {
            std::string path = (const char*)payload->Data;

            // Create node
            auto* node = m_Graph->AddNode(MaterialNodeType::Texture2D);
            if (node)
            {
                auto* texNode = static_cast<Texture2DNode*>(node);
                auto* pathProp = texNode->GetProperty<FilePathProperty>();
                if (pathProp)
                    pathProp->path = path;

                // Set position relative to canvas (accounting for scroll and zoom)
                ImVec2 mousePos = ImGui::GetMousePos();
                texNode->posX = (mousePos.x - canvasPos.x - m_ScrollOffset.x) / m_Zoom;
                texNode->posY = (mousePos.y - canvasPos.y - m_ScrollOffset.y) / m_Zoom;

                // Align to grid if enabled
                if (m_SnapToGrid)
                {
                    texNode->posX = SnapToGrid(texNode->posX);
                    texNode->posY = SnapToGrid(texNode->posY);
                }
            }
        }
        ImGui::EndDragDropTarget();
    }

    bool canvasHovered = ImGui::IsItemHovered();
    bool canvasActive = ImGui::IsItemActive();

    // Capture mouse wheel to prevent zoom pass-through
    if (canvasHovered)
    {
        // Block scroll events from propagating
        ImGui::GetIO().WantCaptureMouse = true;

        // Handle zoom with mouse wheel
        float wheel = ImGui::GetIO().MouseWheel;
        if (wheel != 0.0f)
        {
            // Get mouse position relative to canvas
            ImVec2 mousePos = ImGui::GetMousePos();
            ImVec2 mouseCanvasPos(mousePos.x - canvasPos.x, mousePos.y - canvasPos.y);

            // Calculate zoom
            float zoomDelta = wheel * 0.1f;
            float oldZoom = m_Zoom;
            m_Zoom = std::clamp(m_Zoom + zoomDelta, 0.25f, 3.0f);

            // Zoom toward mouse cursor
            if (oldZoom != m_Zoom)
            {
                float zoomRatio = m_Zoom / oldZoom;
                m_ScrollOffset.x = mouseCanvasPos.x - (mouseCanvasPos.x - m_ScrollOffset.x) * zoomRatio;
                m_ScrollOffset.y = mouseCanvasPos.y - (mouseCanvasPos.y - m_ScrollOffset.y) * zoomRatio;
            }

            // Consume the wheel event to prevent pass-through
            ImGui::GetIO().MouseWheel = 0.0f;
            ImGui::GetIO().MouseWheelH = 0.0f;
        }
    }

    // Handle canvas panning with middle mouse OR Alt+Left mouse (for laptops)
    bool isPanning = ImGui::IsMouseDragging(ImGuiMouseButton_Middle, 0.0f) ||
                     (ImGui::GetIO().KeyAlt && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0f));

    if ((canvasHovered || canvasActive) && isPanning)
    {
        m_ScrollOffset.x += ImGui::GetIO().MouseDelta.x;
        m_ScrollOffset.y += ImGui::GetIO().MouseDelta.y;
    }

    // Clip to canvas
    drawList->PushClipRect(canvasPos, ImVec2(canvasPos.x + canvasSize.x, canvasPos.y + canvasSize.y), true);

    // Draw grid
    DrawGrid(drawList, canvasPos, canvasSize);

    // Draw connections first (behind nodes)
    DrawConnections(drawList, canvasPos);

    // Draw nodes
    DrawNodes(drawList, canvasPos, canvasSize);

    // Draw connection being created
    if (m_CreatingConnection && m_ConnectionStartPin >= 0)
    {
        ImVec2 startPos = GetPinScreenPosition(m_ConnectionStartPin, canvasPos);
        ImVec2 endPos = ImGui::GetMousePos();
        DrawBezierConnection(drawList, startPos, endPos, NodeColors::ConnectionHovered);
    }

    drawList->PopClipRect();

    // Handle tools based on current selection
    switch (m_CurrentTool)
    {
        case GraphTool::Select:
        {
            // Handle click on empty space to deselect
            if (canvasHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && m_HoveredPin < 0)
            {
                // Check if we clicked on a node
                bool clickedNode = false;
                ImVec2 mousePos = ImGui::GetMousePos();

                for (const auto& node : m_Graph->GetNodes())
                {
                    float scaledX = node->posX * m_Zoom + m_ScrollOffset.x;
                    float scaledY = node->posY * m_Zoom + m_ScrollOffset.y;
                    ImVec2 nodePos(canvasPos.x + scaledX, canvasPos.y + scaledY);
                    float nodeWidth = NODE_WIDTH * m_Zoom;
                    float nodeHeight = GetNodeHeight(node.get()) * m_Zoom;

                    if (mousePos.x >= nodePos.x && mousePos.x <= nodePos.x + nodeWidth && mousePos.y >= nodePos.y &&
                        mousePos.y <= nodePos.y + nodeHeight)
                    {
                        m_SelectedNodeId = node->GetId();
                        m_DraggingNode = node->GetId();
                        clickedNode = true;
                        break;
                    }
                }

                if (!clickedNode)
                    m_SelectedNodeId = -1;
            }
        }
        break;

        case GraphTool::Knife:
            HandleKnifeTool(canvasHovered, canvasPos, drawList);
            break;

        case GraphTool::BoxSelect:
            HandleBoxSelectTool(canvasHovered, canvasPos);
            DrawBoxSelection(drawList);
            break;
    }

    // Handle node dragging
    if (m_DraggingNode >= 0 && ImGui::IsMouseDragging(ImGuiMouseButton_Left) && !isPanning)
    {
        if (auto* node = m_Graph->GetNode(m_DraggingNode))
        {
            // Divide by zoom to convert screen-space delta to node-space delta
            node->posX += ImGui::GetIO().MouseDelta.x / m_Zoom;
            node->posY += ImGui::GetIO().MouseDelta.y / m_Zoom;
        }
    }

    if (ImGui::IsMouseReleased(ImGuiMouseButton_Left))
    {
        // Finish connection if hovering over a pin
        if (m_CreatingConnection && m_HoveredPin >= 0 && m_HoveredPin != m_ConnectionStartPin)
        {
            // Determine which is input and which is output
            m_Graph->Connect(m_ConnectionStartPin, m_HoveredPin);
        }

        m_DraggingNode = -1;
        m_CreatingConnection = false;
        m_ConnectionStartPin = -1;
    }

    // Delete selected node with Delete key
    if (m_SelectedNodeId >= 0 && ImGui::IsKeyPressed(ImGuiKey_Delete))
    {
        m_Graph->RemoveNode(m_SelectedNodeId);
        m_SelectedNodeId = -1;
    }

    // Right-click context menu for adding nodes
    if (canvasHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
    {
        // Store position for node creation (convert to graph space)
        ImVec2 mousePos = ImGui::GetMousePos();
        m_PopupPosX = (mousePos.x - canvasPos.x - m_ScrollOffset.x) / m_Zoom;
        m_PopupPosY = (mousePos.y - canvasPos.y - m_ScrollOffset.y) / m_Zoom;
        m_OpenNodePopup = true;
        m_NodeSearchQuery[0] = '\0'; // Clear search
        m_FocusSearch = true;        // Focus on next frame
    }

    // Node definitions for searchable menu
    struct NodeDefinition
    {
        const char* name;
        const char* category;
        MaterialNodeType type;
    };

    static const NodeDefinition s_NodeDefinitions[] = {{"PBR Output", "Output", MaterialNodeType::PBROutput},
                                                       {"Float", "Input", MaterialNodeType::ConstFloat},
                                                       {"Color (Vec3)", "Input", MaterialNodeType::ConstVec3},
                                                       {"Color (Vec4)", "Input", MaterialNodeType::ConstVec4},
                                                       {"Texture 2D", "Input", MaterialNodeType::Texture2D},
                                                       {"Texture Coord", "Input", MaterialNodeType::TextureCoord},
                                                       {"Add", "Math", MaterialNodeType::Add},
                                                       {"Subtract", "Math", MaterialNodeType::Subtract},
                                                       {"Multiply", "Math", MaterialNodeType::Multiply},
                                                       {"Divide", "Math", MaterialNodeType::Divide},
                                                       {"Power", "Math", MaterialNodeType::Power},
                                                       {"Min", "Math", MaterialNodeType::Min},
                                                       {"Max", "Math", MaterialNodeType::Max},
                                                       {"Abs", "Math", MaterialNodeType::Abs},
                                                       {"Negate", "Math", MaterialNodeType::Negate},
                                                       {"One Minus", "Math", MaterialNodeType::OneMinus},
                                                       {"Saturate", "Math", MaterialNodeType::Saturate},
                                                       {"Sin", "Math", MaterialNodeType::Sin},
                                                       {"Cos", "Math", MaterialNodeType::Cos},
                                                       {"Frac", "Math", MaterialNodeType::Frac},
                                                       {"Floor", "Math", MaterialNodeType::Floor},
                                                       {"Ceil", "Math", MaterialNodeType::Ceil},
                                                       {"Sqrt", "Math", MaterialNodeType::Sqrt},
                                                       {"Lerp", "Math", MaterialNodeType::Lerp},
                                                       {"Clamp", "Math", MaterialNodeType::Clamp},
                                                       {"Smoothstep", "Math", MaterialNodeType::Smoothstep},
                                                       {"Split Vec3", "Vector", MaterialNodeType::SplitVec3},
                                                       {"Make Vec3", "Vector", MaterialNodeType::MakeVec3},
                                                       {"Normalize", "Vector", MaterialNodeType::Normalize},
                                                       {"Dot Product", "Vector", MaterialNodeType::Dot},
                                                       {"Cross Product", "Vector", MaterialNodeType::CrossProduct},
                                                       {"Time", "Utility", MaterialNodeType::Time},
                                                       {"Fresnel", "Utility", MaterialNodeType::Fresnel},
                                                       {"Panner", "Utility", MaterialNodeType::Panner},
                                                       {"Perlin Noise", "Procedural", MaterialNodeType::Perlin}};

    // Draw the popup
    if (m_OpenNodePopup)
    {
        ImGui::OpenPopup("AddNodePopup");
        m_OpenNodePopup = false;
    }

    if (ImGui::BeginPopup("AddNodePopup"))
    {
        ImGui::TextDisabled("Search Nodes...");
        if (m_FocusSearch)
        {
            ImGui::SetKeyboardFocusHere();
            m_FocusSearch = false;
        }

        ImGui::PushItemWidth(200);
        ImGui::InputText("##NodeSearch", m_NodeSearchQuery, sizeof(m_NodeSearchQuery));
        ImGui::PopItemWidth();
        ImGui::Separator();

        std::string query = m_NodeSearchQuery;
        std::transform(query.begin(), query.end(), query.begin(), ::tolower);

        bool isSearching = !query.empty();

        if (isSearching)
        {
            // Show filtered flat list
            for (const auto& def : s_NodeDefinitions)
            {
                std::string name = def.name;
                std::transform(name.begin(), name.end(), name.begin(), ::tolower);

                if (name.find(query) != std::string::npos)
                {
                    if (ImGui::MenuItem(def.name))
                    {
                        auto* node = m_Graph->AddNode(def.type);
                        node->posX = m_PopupPosX;
                        node->posY = m_PopupPosY;
                        ImGui::CloseCurrentPopup();
                    }
                }
            }
        }
        else
        {
            // Show categorized menus
            const char* lastCategory = nullptr;
            bool categoryOpen = false;

            for (const auto& def : s_NodeDefinitions)
            {
                if (lastCategory == nullptr || std::strcmp(lastCategory, def.category) != 0)
                {
                    if (categoryOpen)
                    {
                        ImGui::EndMenu();
                        categoryOpen = false;
                    }

                    if (ImGui::BeginMenu(def.category))
                    {
                        categoryOpen = true;
                    }
                    lastCategory = def.category;
                }

                if (categoryOpen)
                {
                    if (ImGui::MenuItem(def.name))
                    {
                        auto* node = m_Graph->AddNode(def.type);
                        node->posX = m_PopupPosX;
                        node->posY = m_PopupPosY;
                    }
                }
            }
            if (categoryOpen)
                ImGui::EndMenu();
        }

        ImGui::EndPopup();
    }

    // Draw Stats Overlay (Unreal-style)
    {
        ImVec2 statsSize(200, 70);
        ImVec2 statsPos(canvasPos.x + 10, canvasPos.y + canvasSize.y - statsSize.y - 10);

        int nodeCount = static_cast<int>(m_Graph->GetNodes().size());

        // Very rough "instruction count" estimation
        int mathNodes = 0;
        for (const auto& n : m_Graph->GetNodes())
        {
            if (n->GetType() != MaterialNodeType::PBROutput && n->GetType() != MaterialNodeType::ConstFloat &&
                n->GetType() != MaterialNodeType::ConstVec3)
                mathNodes++;
        }

        char statsBuf[128];
        std::snprintf(statsBuf, sizeof(statsBuf), "Nodes: %d\nEst. Instructions: ~%d\nZoom: %.1f%%", nodeCount,
                      mathNodes * 2 + 5, m_Zoom * 100.0f);

        drawList->AddRectFilled(statsPos, ImVec2(statsPos.x + statsSize.x, statsPos.y + statsSize.y),
                                IM_COL32(0, 0, 0, 150), 4.0f);
        drawList->AddRect(statsPos, ImVec2(statsPos.x + statsSize.x, statsPos.y + statsSize.y),
                          IM_COL32(100, 100, 100, 200), 4.0f);
        drawList->AddText(ImVec2(statsPos.x + 10, statsPos.y + 10), IM_COL32(220, 220, 220, 255), "GRAPH STATS");
        drawList->AddText(ImVec2(statsPos.x + 10, statsPos.y + 30), IM_COL32(180, 180, 180, 255), statsBuf);
    }
}

void MaterialGraphPanel::DrawGrid(ImDrawList* drawList, const ImVec2& canvasPos, const ImVec2& canvasSize)
{
    // Background
    drawList->AddRectFilled(canvasPos, ImVec2(canvasPos.x + canvasSize.x, canvasPos.y + canvasSize.y),
                            NodeColors::Grid);

    // Grid lines (scaled by zoom)
    float scaledGridSize = GRID_SIZE * m_Zoom;
    float offsetX = fmodf(m_ScrollOffset.x, scaledGridSize);
    float offsetY = fmodf(m_ScrollOffset.y, scaledGridSize);

    for (float x = offsetX; x < canvasSize.x; x += scaledGridSize)
    {
        drawList->AddLine(ImVec2(canvasPos.x + x, canvasPos.y), ImVec2(canvasPos.x + x, canvasPos.y + canvasSize.y),
                          NodeColors::GridLine);
    }

    for (float y = offsetY; y < canvasSize.y; y += scaledGridSize)
    {
        drawList->AddLine(ImVec2(canvasPos.x, canvasPos.y + y), ImVec2(canvasPos.x + canvasSize.x, canvasPos.y + y),
                          NodeColors::GridLine);
    }
}

void MaterialGraphPanel::DrawNodes(ImDrawList* drawList, const ImVec2& canvasPos, const ImVec2& canvasSize)
{
    m_HoveredPin = -1;
    ImVec2 mousePos = ImGui::GetMousePos();

    // Scaled dimensions
    float nodeWidth = NODE_WIDTH * m_Zoom;
    float titleHeight = NODE_TITLE_HEIGHT * m_Zoom;
    float pinSpacing = NODE_PIN_SPACING * m_Zoom;
    float pinRadius = NODE_PIN_RADIUS * m_Zoom;
    float nodePadding = NODE_PADDING * m_Zoom;
    float cornerRadius = 6.0f * m_Zoom;
    float textOffsetX = 8.0f * m_Zoom;
    float textOffsetY = 5.0f * m_Zoom;
    const float visibilityPadding = 96.0f * m_Zoom;
    const ImVec2 visibleMin(canvasPos.x - visibilityPadding, canvasPos.y - visibilityPadding);
    const ImVec2 visibleMax(canvasPos.x + canvasSize.x + visibilityPadding, canvasPos.y + canvasSize.y + visibilityPadding);

    for (const auto& node : m_Graph->GetNodes())
    {
        // Apply zoom to node position
        float scaledX = node->posX * m_Zoom + m_ScrollOffset.x;
        float scaledY = node->posY * m_Zoom + m_ScrollOffset.y;
        ImVec2 nodePos(canvasPos.x + scaledX, canvasPos.y + scaledY);

        float nodeHeight = GetNodeHeight(node.get()) * m_Zoom;
        const ImVec2 nodeMax(nodePos.x + nodeWidth, nodePos.y + nodeHeight);
        const bool nodeVisible =
            nodePos.x <= visibleMax.x && nodeMax.x >= visibleMin.x && nodePos.y <= visibleMax.y && nodeMax.y >= visibleMin.y;
        if (!nodeVisible)
            continue;

        bool isSelected = (node->GetId() == m_SelectedNodeId);
        bool isHovered = (mousePos.x >= nodePos.x && mousePos.x <= nodePos.x + nodeWidth && mousePos.y >= nodePos.y &&
                          mousePos.y <= nodePos.y + nodeHeight);

        // Node background
        ImU32 bgColor =
            isSelected ? NodeColors::NodeBgSelected : (isHovered ? NodeColors::NodeBgHovered : NodeColors::NodeBg);
        ImU32 borderColor = isSelected ? NodeColors::NodeBorderSelected : NodeColors::NodeBorder;

        // Determine title bar color by category
        ImU32 titleBarColor = NodeColors::TitleBar;
        switch (node->GetType())
        {
            case MaterialNodeType::PBROutput:
                titleBarColor = NodeColors::CatOutput;
                break;
            case MaterialNodeType::ConstFloat:
            case MaterialNodeType::ConstVec3:
            case MaterialNodeType::ConstVec4:
                titleBarColor = NodeColors::CatConstant;
                break;
            case MaterialNodeType::Texture2D:
            case MaterialNodeType::TextureCoord:
                titleBarColor = NodeColors::CatTexture;
                break;
            case MaterialNodeType::Time:
            case MaterialNodeType::Panner:
            case MaterialNodeType::Fresnel:
                titleBarColor = NodeColors::CatUtility;
                break;
            case MaterialNodeType::Perlin:
                titleBarColor = NodeColors::CatProcedural;
                break;
            default:
                titleBarColor = NodeColors::CatMath;
                break;
        }

        drawList->AddRectFilled(nodePos, ImVec2(nodePos.x + nodeWidth, nodePos.y + nodeHeight), bgColor, cornerRadius);
        drawList->AddRect(nodePos, ImVec2(nodePos.x + nodeWidth, nodePos.y + nodeHeight), borderColor, cornerRadius, 0,
                          2.0f);

        // Title bar
        drawList->AddRectFilled(nodePos, ImVec2(nodePos.x + nodeWidth, nodePos.y + titleHeight), titleBarColor,
                                cornerRadius, ImDrawFlags_RoundCornersTop);

        // Title text
        drawList->AddText(ImVec2(nodePos.x + textOffsetX, nodePos.y + textOffsetY), NodeColors::TitleText,
                          node->GetName().c_str());

        // Draw input pins
        int pinIdx = 0;
        for (const auto& pin : node->GetInputs())
        {
            float pinY = nodePos.y + titleHeight + pinSpacing * (pinIdx + 1);
            ImVec2 pinPos(nodePos.x, pinY);

            DrawPin(drawList, pinPos, pin, mousePos, pinRadius);

            // Pin label
            drawList->AddText(ImVec2(pinPos.x + pinRadius * 2.0f, pinPos.y - 7 * m_Zoom), IM_COL32(200, 200, 200, 255),
                              pin.name.c_str());

            pinIdx++;
        }

        // Draw output pins
        pinIdx = 0;
        for (const auto& pin : node->GetOutputs())
        {
            float pinY = nodePos.y + titleHeight + pinSpacing * (pinIdx + 1);
            ImVec2 pinPos(nodePos.x + nodeWidth, pinY);

            DrawPin(drawList, pinPos, pin, mousePos, pinRadius);

            // Pin label (right-aligned)
            ImVec2 textSize = ImGui::CalcTextSize(pin.name.c_str());
            drawList->AddText(ImVec2(pinPos.x - pinRadius * 2.0f - textSize.x, pinPos.y - 7 * m_Zoom),
                              IM_COL32(200, 200, 200, 255), pin.name.c_str());

            pinIdx++;
        }

        // Draw Previews (Visual Debugging)
        int maxPins = std::max((int)node->GetInputs().size(), (int)node->GetOutputs().size());
        float previewY = nodePos.y + titleHeight + pinSpacing * (maxPins + 1) + nodePadding;

        if (node->GetType() == MaterialNodeType::ConstVec3 || node->GetType() == MaterialNodeType::ConstVec4)
        {
            // Show color swatch
            auto* colorProp = node->GetProperty<Vec3Property>();
            if (colorProp)
            {
                ImVec2 p1(nodePos.x + nodePadding, previewY);
                ImVec2 p2(nodePos.x + nodeWidth - nodePadding, previewY + 25.0f * m_Zoom);
                ImU32 col = IM_COL32((int)(colorProp->value.x * 255), (int)(colorProp->value.y * 255),
                                     (int)(colorProp->value.z * 255), 255);
                drawList->AddRectFilled(p1, p2, col, 4.0f * m_Zoom);
                drawList->AddRect(p1, p2, IM_COL32(255, 255, 255, 50), 4.0f * m_Zoom);
            }
        }
        else if (node->GetType() == MaterialNodeType::ConstFloat)
        {
            auto* floatProp = node->GetProperty<FloatProperty>();
            if (floatProp)
            {
                char buf[32];
                std::snprintf(buf, sizeof(buf), "Value: %.2f", floatProp->value);
                drawList->AddText(ImVec2(nodePos.x + nodePadding, previewY), IM_COL32(180, 180, 180, 255), buf);
            }
        }
        else if (node->GetType() == MaterialNodeType::Texture2D)
        {
            auto* pathProp = node->GetProperty<FilePathProperty>();
            if (pathProp)
            {
                std::string filename =
                    pathProp->path.empty() ? "(none)" : std::filesystem::path(pathProp->path).filename().string();

                // Draw a little box representing a texture
                ImVec2 p1(nodePos.x + nodePadding, previewY);
                ImVec2 p2(nodePos.x + nodePadding + 64.0f * m_Zoom, previewY + 64.0f * m_Zoom);

                void* texId = GetGPUThumbnail(pathProp->path);
                if (texId)
                {
                    drawList->AddImage(texId, p1, p2);
                }
                else
                {
                    drawList->AddRectFilled(p1, p2, IM_COL32(30, 30, 35, 255), 2.0f);
                    drawList->AddRect(p1, p2, IM_COL32(80, 80, 90, 255), 2.0f);
                    // "TEX" label inside box
                    drawList->AddText(ImVec2(p1.x + 15, p1.y + 25), IM_COL32(100, 100, 110, 255), "TEX");
                }

                // Filename next to it or below
                drawList->AddText(ImVec2(p2.x + 5, p1.y + 25), IM_COL32(180, 180, 180, 255), filename.c_str());
            }
        }
    }
}

void MaterialGraphPanel::DrawPin(ImDrawList* drawList, const ImVec2& pos, const MaterialPin& pin,
                                 const ImVec2& mousePos, float pinRadius)
{
    ImU32 color = GetPinColor(pin.type);

    // Check if hovered (with zoom-aware radius)
    float dist = sqrtf((mousePos.x - pos.x) * (mousePos.x - pos.x) + (mousePos.y - pos.y) * (mousePos.y - pos.y));
    bool hovered = dist <= pinRadius + 4.0f;

    if (hovered)
    {
        m_HoveredPin = pin.id;

        // Start connection on click
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        {
            m_CreatingConnection = true;
            m_ConnectionStartPin = pin.id;
            m_ConnectionStartIsInput = pin.isInput;
        }
    }

    // Draw pin circle
    float radius = hovered ? pinRadius + 2.0f : pinRadius;

    // Filled if connected, outline if not
    if (pin.linkedPinId >= 0)
    {
        drawList->AddCircleFilled(pos, radius, color);
    }
    else
    {
        drawList->AddCircle(pos, radius, color, 12, 2.0f);
        drawList->AddCircleFilled(pos, radius * 0.5f, color);
    }

    // Highlight ring when hovered
    if (hovered)
    {
        drawList->AddCircle(pos, radius + 2.0f, IM_COL32(255, 255, 255, 150), 12, 2.0f);

        // Pin Tooltip (Unreal-style)
        ImGui::BeginTooltip();
        const char* typeName = "Unknown";
        switch (pin.type)
        {
            case MaterialPinType::Float:
                typeName = "Float";
                break;
            case MaterialPinType::Vec2:
                typeName = "Vec2";
                break;
            case MaterialPinType::Vec3:
                typeName = "Vec3";
                break;
            case MaterialPinType::Vec4:
                typeName = "Vec4";
                break;
            case MaterialPinType::Texture:
                typeName = "Sampler2D";
                break;
        }
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(color), "[%s]", typeName);
        ImGui::SameLine();
        ImGui::Text("%s", pin.name.c_str());
        if (pin.linkedPinId < 0 && pin.isInput)
        {
            if (pin.type == MaterialPinType::Float)
                ImGui::TextDisabled("Default: %.2f", pin.defaultFloat);
            else if (pin.type == MaterialPinType::Vec3)
                ImGui::TextDisabled("Default: (%.1f, %.1f, %.1f)", pin.defaultVec3.x, pin.defaultVec3.y,
                                    pin.defaultVec3.z);
        }
        ImGui::EndTooltip();
    }
}

void MaterialGraphPanel::DrawConnections(ImDrawList* drawList, const ImVec2& canvasPos)
{
    for (const auto& conn : m_Graph->GetConnections())
    {
        ImVec2 startPos = GetPinScreenPosition(conn.outputPinId, canvasPos);
        ImVec2 endPos = GetPinScreenPosition(conn.inputPinId, canvasPos);

        if (startPos.x > 0 && endPos.x > 0) // Valid positions
        {
            DrawBezierConnection(drawList, startPos, endPos, NodeColors::Connection);
        }
    }
}

void MaterialGraphPanel::DrawBezierConnection(ImDrawList* drawList, const ImVec2& start, const ImVec2& end, ImU32 color)
{
    // Calculate control points for a nice bezier curve
    float dist = fabsf(end.x - start.x) * 0.5f;
    dist = std::max(dist, 50.0f * m_Zoom); // Minimum curve distance (scaled)

    ImVec2 cp1(start.x + dist, start.y);
    ImVec2 cp2(end.x - dist, end.y);

    drawList->AddBezierCubic(start, cp1, cp2, end, color, 2.5f * m_Zoom);
}

ImVec2 MaterialGraphPanel::GetPinScreenPosition(int pinId, const ImVec2& canvasPos)
{
    // Scaled dimensions (same as in DrawNodes)
    float nodeWidth = NODE_WIDTH * m_Zoom;
    float titleHeight = NODE_TITLE_HEIGHT * m_Zoom;
    float pinSpacing = NODE_PIN_SPACING * m_Zoom;

    for (const auto& node : m_Graph->GetNodes())
    {
        // Apply zoom to node position (same as in DrawNodes)
        float scaledX = node->posX * m_Zoom + m_ScrollOffset.x;
        float scaledY = node->posY * m_Zoom + m_ScrollOffset.y;
        ImVec2 nodePos(canvasPos.x + scaledX, canvasPos.y + scaledY);

        int idx = 0;
        for (const auto& pin : node->GetInputs())
        {
            if (pin.id == pinId)
            {
                float pinY = nodePos.y + titleHeight + pinSpacing * (idx + 1);
                return ImVec2(nodePos.x, pinY);
            }
            idx++;
        }

        idx = 0;
        for (const auto& pin : node->GetOutputs())
        {
            if (pin.id == pinId)
            {
                float pinY = nodePos.y + titleHeight + pinSpacing * (idx + 1);
                return ImVec2(nodePos.x + nodeWidth, pinY);
            }
            idx++;
        }
    }

    return ImVec2(-1, -1);
}

void MaterialGraphPanel::DrawPropertyPanel()
{
    ImGui::Text("Properties");
    ImGui::Separator();

    if (m_SelectedNodeId < 0)
    {
        ImGui::TextDisabled("Select a node");
        return;
    }

    auto* node = m_Graph->GetNode(m_SelectedNodeId);
    if (!node)
        return;

    ImGui::Text("Node: %s", node->GetName().c_str());
    ImGui::Text("ID: %d", node->GetId());
    // All node properties are now handled by the auto-property rendering system below

    // Auto-render properties from the composable property system
    const auto& properties = node->GetProperties();
    if (!properties.empty())
    {
        ImGui::Separator();
        ImGui::Text("Properties (Composable)");

        for (const auto& prop : properties)
        {
            switch (prop->GetType())
            {
                case PropertyType::Float:
                {
                    auto* floatProp = dynamic_cast<FloatProperty*>(prop.get());
                    if (floatProp)
                    {
                        ImGui::DragFloat(floatProp->GetName().c_str(), &floatProp->value, 0.01f, floatProp->minValue,
                                         floatProp->maxValue);
                    }
                }
                break;

                case PropertyType::Int:
                {
                    auto* intProp = dynamic_cast<IntProperty*>(prop.get());
                    if (intProp)
                    {
                        ImGui::DragInt(intProp->GetName().c_str(), &intProp->value, 1, intProp->minValue,
                                       intProp->maxValue);
                    }
                }
                break;

                case PropertyType::Vec3:
                {
                    auto* vec3Prop = dynamic_cast<Vec3Property*>(prop.get());
                    if (vec3Prop)
                    {
                        float color[3] = {vec3Prop->value.x, vec3Prop->value.y, vec3Prop->value.z};
                        if (ImGui::ColorEdit3(vec3Prop->GetName().c_str(), color))
                        {
                            vec3Prop->value = Vec3(color[0], color[1], color[2]);
                        }
                    }
                    // Check for TilingProperty or OffsetProperty
                    auto* tilingProp = dynamic_cast<TilingProperty*>(prop.get());
                    if (tilingProp)
                    {
                        float tiling[2] = {tilingProp->tilingU, tilingProp->tilingV};
                        if (ImGui::DragFloat2(tilingProp->GetName().c_str(), tiling, 0.1f, 0.01f, 100.0f))
                        {
                            tilingProp->tilingU = tiling[0];
                            tilingProp->tilingV = tiling[1];
                        }
                    }
                    auto* offsetProp = dynamic_cast<OffsetProperty*>(prop.get());
                    if (offsetProp)
                    {
                        float offset[2] = {offsetProp->offsetU, offsetProp->offsetV};
                        if (ImGui::DragFloat2(offsetProp->GetName().c_str(), offset, 0.01f, -10.0f, 10.0f))
                        {
                            offsetProp->offsetU = offset[0];
                            offsetProp->offsetV = offset[1];
                        }
                    }
                    // Check for PannerSpeedProperty
                    auto* pannerSpeedProp = dynamic_cast<PannerSpeedProperty*>(prop.get());
                    if (pannerSpeedProp)
                    {
                        float speed[2] = {pannerSpeedProp->speedU, pannerSpeedProp->speedV};
                        if (ImGui::DragFloat2(pannerSpeedProp->GetName().c_str(), speed, 0.01f, -5.0f, 5.0f))
                        {
                            pannerSpeedProp->speedU = speed[0];
                            pannerSpeedProp->speedV = speed[1];
                        }
                    }
                }
                break;

                case PropertyType::Bool:
                {
                    auto* boolProp = dynamic_cast<BoolProperty*>(prop.get());
                    if (boolProp)
                    {
                        ImGui::Checkbox(boolProp->GetName().c_str(), &boolProp->value);
                    }
                }
                break;

                case PropertyType::FilePath:
                {
                    auto* pathProp = dynamic_cast<FilePathProperty*>(prop.get());
                    if (pathProp)
                    {
                        // Show property name and current filename
                        std::string displayName = pathProp->path.empty()
                                                      ? "(No file selected)"
                                                      : std::filesystem::path(pathProp->path).filename().string();
                        ImGui::Text("%s: %s", pathProp->GetName().c_str(), displayName.c_str());

                        // Browse button
                        if (ImGui::Button("Browse..."))
                        {
                            // Use image file filter for texture selection
                            FileFilter filter = {"Image Files", "*.png;*.jpg;*.jpeg;*.bmp;*.dds"};
                            std::string result = FileDialogs::OpenFile(filter);
                            if (!result.empty())
                            {
                                pathProp->path = result;
                                if (node->GetType() == MaterialNodeType::Texture2D)
                                {
                                    if (auto* sampleTypeProp = node->GetProperty<TextureSampleTypeProperty>())
                                    {
                                        sampleTypeProp->value =
                                            static_cast<int>(GuessTextureSampleTypeFromPath(pathProp->path));
                                    }
                                    AutoRouteTextureNodeToPbrInput(m_Graph.get(), node);
                                }
                            }
                        }
                        ImGui::SameLine();
                        if (!pathProp->path.empty() && ImGui::Button("Clear"))
                        {
                            pathProp->path.clear();
                        }
                    }
                }
                break;

                case PropertyType::Enum:
                {
                    auto* enumProp = dynamic_cast<EnumProperty*>(prop.get());
                    if (enumProp)
                    {
                        const auto& options = enumProp->GetOptions();
                        if (ImGui::BeginCombo(enumProp->GetName().c_str(), options[enumProp->value].c_str()))
                        {
                            for (int i = 0; i < static_cast<int>(options.size()); ++i)
                            {
                                bool isSelected = (enumProp->value == i);
                                if (ImGui::Selectable(options[i].c_str(), isSelected))
                                {
                                    enumProp->value = i;
                                    if (node->GetType() == MaterialNodeType::Texture2D &&
                                        enumProp->GetName() == "Sample Type")
                                    {
                                        AutoRouteTextureNodeToPbrInput(m_Graph.get(), node);
                                    }
                                }
                                if (isSelected)
                                    ImGui::SetItemDefaultFocus();
                            }
                            ImGui::EndCombo();
                        }
                    }
                }
                break;
            }
        }
    }
}

void MaterialGraphPanel::DrawCodePreview()
{
    ImGui::SetNextWindowSize(ImVec2(600, 400), ImGuiCond_FirstUseEver);
    if (BeginChromeWindow("Generated HLSL", &m_ShowCodePreview))
    {
        std::string code = GetGeneratedCode();
        ImGui::TextWrapped("%s", code.c_str());
    }
    ImGui::End();
}

void MaterialGraphPanel::DrawNodeEditor()
{ /* Legacy - replaced by DrawNodeCanvas */
}
void MaterialGraphPanel::HandleNodeCreation() {}
void MaterialGraphPanel::HandleConnections() {}

// =============================================================================
// Tool Functions
// =============================================================================

void MaterialGraphPanel::HandleKnifeTool(bool canvasHovered, const ImVec2& canvasPos, ImDrawList* drawList)
{
    ImVec2 mousePos = ImGui::GetMousePos();

    // Start knife drag
    if (canvasHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
    {
        m_KnifeActive = true;
        m_KnifeTrail.clear();
        m_KnifeTrail.push_back(mousePos);
    }

    // Continue knife drag - add points to trail
    if (m_KnifeActive && ImGui::IsMouseDown(ImGuiMouseButton_Left))
    {
        if (!m_KnifeTrail.empty())
        {
            ImVec2 lastPoint = m_KnifeTrail.back();
            float dist = sqrtf((mousePos.x - lastPoint.x) * (mousePos.x - lastPoint.x) +
                               (mousePos.y - lastPoint.y) * (mousePos.y - lastPoint.y));
            // Add point if moved enough
            if (dist > 5.0f)
            {
                m_KnifeTrail.push_back(mousePos);
            }
        }
    }

    // End knife drag - check for intersections and cut connections
    if (m_KnifeActive && ImGui::IsMouseReleased(ImGuiMouseButton_Left))
    {
        // Check each segment of the knife trail against each connection
        std::vector<int> connectionsToRemove;

        for (size_t i = 0; i < m_KnifeTrail.size() - 1; ++i)
        {
            ImVec2 lineStart = m_KnifeTrail[i];
            ImVec2 lineEnd = m_KnifeTrail[i + 1];

            // Check against all connections
            const auto& connections = m_Graph->GetConnections();
            for (size_t c = 0; c < connections.size(); ++c)
            {
                const auto& conn = connections[c];
                ImVec2 pinStart = GetPinScreenPosition(conn.outputPinId, canvasPos);
                ImVec2 pinEnd = GetPinScreenPosition(conn.inputPinId, canvasPos);

                if (LineIntersectsBezier(lineStart, lineEnd, pinStart, pinEnd))
                {
                    // Mark for removal (avoid modifying during iteration)
                    bool alreadyMarked = false;
                    for (int id : connectionsToRemove)
                        if (id == static_cast<int>(c))
                        {
                            alreadyMarked = true;
                            break;
                        }
                    if (!alreadyMarked)
                        connectionsToRemove.push_back(conn.id); // Store connection ID, not index
                }
            }
        }

        // Remove connections by ID
        for (int connId : connectionsToRemove)
        {
            m_Graph->Disconnect(connId);
        }

        m_KnifeActive = false;
        m_KnifeTrail.clear();
    }

    // Draw knife trail
    DrawKnifeTrail(drawList);
}

void MaterialGraphPanel::DrawKnifeTrail(ImDrawList* drawList)
{
    if (!m_KnifeActive || m_KnifeTrail.size() < 2)
        return;

    // Orange-red color for knife trail
    ImU32 knifeColor = IM_COL32(255, 100, 50, 200);
    ImU32 knifeGlow = IM_COL32(255, 150, 50, 80);

    // Draw glow effect
    for (size_t i = 0; i < m_KnifeTrail.size() - 1; ++i)
    {
        drawList->AddLine(m_KnifeTrail[i], m_KnifeTrail[i + 1], knifeGlow, 6.0f * m_Zoom);
    }

    // Draw main line
    for (size_t i = 0; i < m_KnifeTrail.size() - 1; ++i)
    {
        drawList->AddLine(m_KnifeTrail[i], m_KnifeTrail[i + 1], knifeColor, 2.5f * m_Zoom);
    }

    // Draw endpoint marker
    if (!m_KnifeTrail.empty())
    {
        ImVec2 lastPoint = m_KnifeTrail.back();
        drawList->AddCircleFilled(lastPoint, 4.0f * m_Zoom, knifeColor);
    }
}

bool MaterialGraphPanel::LineIntersectsBezier(const ImVec2& lineStart, const ImVec2& lineEnd, const ImVec2& bezierStart,
                                              const ImVec2& bezierEnd)
{
    // Approximate bezier as line segments and check intersection
    // This is a simplified version - we sample the bezier at several points

    float dist = fabsf(bezierEnd.x - bezierStart.x) * 0.5f;
    dist = std::max(dist, 50.0f * m_Zoom);

    ImVec2 cp1(bezierStart.x + dist, bezierStart.y);
    ImVec2 cp2(bezierEnd.x - dist, bezierEnd.y);

    // Sample bezier at several points
    const int samples = 10;
    for (int i = 0; i < samples; ++i)
    {
        float t1 = static_cast<float>(i) / samples;
        float t2 = static_cast<float>(i + 1) / samples;

        // Cubic bezier formula
        auto bezierPoint = [&](float t) -> ImVec2
        {
            float u = 1.0f - t;
            return ImVec2(
                u * u * u * bezierStart.x + 3 * u * u * t * cp1.x + 3 * u * t * t * cp2.x + t * t * t * bezierEnd.x,
                u * u * u * bezierStart.y + 3 * u * u * t * cp1.y + 3 * u * t * t * cp2.y + t * t * t * bezierEnd.y);
        };

        ImVec2 p1 = bezierPoint(t1);
        ImVec2 p2 = bezierPoint(t2);

        // Check line-line intersection
        float d1x = lineEnd.x - lineStart.x;
        float d1y = lineEnd.y - lineStart.y;
        float d2x = p2.x - p1.x;
        float d2y = p2.y - p1.y;

        float cross = d1x * d2y - d1y * d2x;
        if (fabsf(cross) < 0.0001f)
            continue; // Parallel

        float t = ((p1.x - lineStart.x) * d2y - (p1.y - lineStart.y) * d2x) / cross;
        float s = ((p1.x - lineStart.x) * d1y - (p1.y - lineStart.y) * d1x) / cross;

        if (t >= 0.0f && t <= 1.0f && s >= 0.0f && s <= 1.0f)
        {
            return true; // Intersection found
        }
    }

    return false;
}

void MaterialGraphPanel::HandleBoxSelectTool(bool canvasHovered, const ImVec2& canvasPos)
{
    ImVec2 mousePos = ImGui::GetMousePos();

    // Start box selection
    if (canvasHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
    {
        m_BoxSelectActive = true;
        m_BoxSelectStart = mousePos;
        m_BoxSelectEnd = mousePos;
        m_SelectedNodeIds.clear();
    }

    // Continue box selection
    if (m_BoxSelectActive && ImGui::IsMouseDown(ImGuiMouseButton_Left))
    {
        m_BoxSelectEnd = mousePos;
    }

    // End box selection - select nodes in box
    if (m_BoxSelectActive && ImGui::IsMouseReleased(ImGuiMouseButton_Left))
    {
        // Determine box bounds
        float minX = std::min(m_BoxSelectStart.x, m_BoxSelectEnd.x);
        float maxX = std::max(m_BoxSelectStart.x, m_BoxSelectEnd.x);
        float minY = std::min(m_BoxSelectStart.y, m_BoxSelectEnd.y);
        float maxY = std::max(m_BoxSelectStart.y, m_BoxSelectEnd.y);

        // Find nodes within box
        m_SelectedNodeIds.clear();
        for (const auto& node : m_Graph->GetNodes())
        {
            float nodeX = canvasPos.x + node->posX * m_Zoom + m_ScrollOffset.x;
            float nodeY = canvasPos.y + node->posY * m_Zoom + m_ScrollOffset.y;
            float nodeWidth = 160.0f * m_Zoom; // NODE_WIDTH
            float nodeHeight = 80.0f * m_Zoom; // Approximate

            // Check if node is within selection box
            if (nodeX + nodeWidth >= minX && nodeX <= maxX && nodeY + nodeHeight >= minY && nodeY <= maxY)
            {
                m_SelectedNodeIds.push_back(node->GetId());
            }
        }

        // Set single selection if only one node
        if (m_SelectedNodeIds.size() == 1)
            m_SelectedNodeId = m_SelectedNodeIds[0];
        else if (!m_SelectedNodeIds.empty())
            m_SelectedNodeId = m_SelectedNodeIds[0]; // First selected

        m_BoxSelectActive = false;
    }
}

void MaterialGraphPanel::DrawBoxSelection(ImDrawList* drawList)
{
    if (!m_BoxSelectActive)
        return;

    ImU32 fillColor = IM_COL32(100, 150, 255, 40);
    ImU32 borderColor = IM_COL32(100, 150, 255, 200);

    drawList->AddRectFilled(m_BoxSelectStart, m_BoxSelectEnd, fillColor);
    drawList->AddRect(m_BoxSelectStart, m_BoxSelectEnd, borderColor, 0.0f, 0, 2.0f);
}

// =============================================================================
// Material Preview
// =============================================================================

Vec3 MaterialGraphPanel::SampleTexture(const std::string& path, float u, float v)
{
    // Try to find in cache
    auto it = m_TextureCache.find(path);
    if (it == m_TextureCache.end())
    {
        // Not in cache, try to load
        TextureCacheEntry entry;
        int w, h, c;
        entry.data = stbi_load(path.c_str(), &w, &h, &c, 4);
        if (entry.data)
        {
            entry.width = w;
            entry.height = h;
            entry.channels = 4;
            m_TextureCache[path] = entry;
            it = m_TextureCache.find(path);
        }
        else
        {
            // Failed to load, insert empty entry to avoid retrying every frame
            m_TextureCache[path] = entry;
            return Vec3(1.0f, 0.0f, 1.0f); // Magenta error color
        }
    }

    const auto& entry = it->second;
    if (!entry.data || entry.width <= 0 || entry.height <= 0)
    {
        return Vec3(1.0f, 0.0f, 1.0f); // Magenta error
    }

    // Wrap UVs to [0, 1)
    u = u - floorf(u);
    v = v - floorf(v);

    // Pixel coordinates (bilinear)
    float px = u * (entry.width - 1);
    float py = v * (entry.height - 1);

    int x0 = static_cast<int>(floorf(px));
    int y0 = static_cast<int>(floorf(py));
    int x1 = std::min(x0 + 1, entry.width - 1);
    int y1 = std::min(y0 + 1, entry.height - 1);

    float fx = px - x0;
    float fy = py - y0;

    // Sample 4 texels
    auto getPixel = [&](int x, int y) -> Vec3
    {
        int idx = (y * entry.width + x) * 4;
        return Vec3(entry.data[idx + 0] / 255.0f, entry.data[idx + 1] / 255.0f, entry.data[idx + 2] / 255.0f);
    };

    Vec3 c00 = getPixel(x0, y0);
    Vec3 c10 = getPixel(x1, y0);
    Vec3 c01 = getPixel(x0, y1);
    Vec3 c11 = getPixel(x1, y1);

    // Bilinear interpolation
    Vec3 c0 = c00 * (1.0f - fx) + c10 * fx;
    Vec3 c1 = c01 * (1.0f - fx) + c11 * fx;
    return c0 * (1.0f - fy) + c1 * fy;
}

void MaterialGraphPanel::EvaluateMaterialOutput(float& r, float& g, float& b, float& metallic, float& roughness,
                                                float u, float v)
{
    // Default values
    r = 0.5f;
    g = 0.5f;
    b = 0.5f;
    metallic = 0.0f;
    roughness = 0.5f;

    if (!m_Graph)
        return;

    // Find PBR Output node
    const MaterialNode* outputNode = nullptr;
    for (const auto& node : m_Graph->GetNodes())
    {
        if (node->GetType() == MaterialNodeType::PBROutput)
        {
            outputNode = node.get();
            break;
        }
    }

    if (!outputNode)
        return;

    // Use EvaluatePin for each output
    for (const auto& pin : outputNode->GetInputs())
    {
        Vec4 val = EvaluatePin(pin.id, u, v);

        if (pin.name == "Albedo" || pin.name == "BaseColor")
        {
            r = val.x;
            g = val.y;
            b = val.z;
        }
        else if (pin.name == "Metallic")
        {
            metallic = val.x;
        }
        else if (pin.name == "Roughness")
        {
            roughness = val.x;
        }
    }
}

Vec4 MaterialGraphPanel::EvaluatePin(int pinId, float u, float v)
{
    if (!m_Graph)
        return Vec4(0, 0, 0, 0);

    int outputPinId = m_Graph->GetConnectedOutputPin(pinId);
    if (outputPinId == -1)
    {
        // Try to get default from pin
        MaterialPin* pin = m_Graph->GetPinById(pinId);
        if (pin)
        {
            if (pin->type == MaterialPinType::Float)
                return Vec4(pin->defaultFloat, pin->defaultFloat, pin->defaultFloat, 1.0f);
            if (pin->type == MaterialPinType::Vec3)
                return Vec4(pin->defaultVec3.x, pin->defaultVec3.y, pin->defaultVec3.z, 1.0f);
        }
        return Vec4(0, 0, 0, 1);
    }

    const MaterialNode* node = m_Graph->GetNodeByPinId(outputPinId);
    if (!node)
        return Vec4(0, 0, 0, 1);

    int outputIdx = node->GetOutputIndex(outputPinId);

    switch (node->GetType())
    {
        case MaterialNodeType::ConstFloat:
        {
            auto* prop = node->GetProperty<FloatProperty>();
            float val = prop ? prop->value : 0.0f;
            return Vec4(val, val, val, val);
        }
        case MaterialNodeType::ConstVec3:
        {
            auto* prop = node->GetProperty<Vec3Property>();
            Vec3 val = prop ? prop->value : Vec3(0, 0, 0);
            return Vec4(val.x, val.y, val.z, 1.0f);
        }
        case MaterialNodeType::Add:
        {
            Vec4 a = EvaluatePin(node->GetInputs()[0].id, u, v);
            Vec4 b = EvaluatePin(node->GetInputs()[1].id, u, v);
            return a + b;
        }
        case MaterialNodeType::Multiply:
        {
            Vec4 a = EvaluatePin(node->GetInputs()[0].id, u, v);
            Vec4 b = EvaluatePin(node->GetInputs()[1].id, u, v);
            return a * b;
        }
        case MaterialNodeType::Lerp:
        {
            Vec4 a = EvaluatePin(node->GetInputs()[0].id, u, v);
            Vec4 b = EvaluatePin(node->GetInputs()[1].id, u, v);
            Vec4 alpha = EvaluatePin(node->GetInputs()[2].id, u, v);
            float t = alpha.x;
            return a * (1.0f - t) + b * t;
        }
        case MaterialNodeType::Smoothstep:
        {
            // Get edge0 from property or connected pin
            float edge0 = 0.0f;
            if (node->GetInputs()[0].linkedPinId != -1)
                edge0 = EvaluatePin(node->GetInputs()[0].id, u, v).x;
            else
            {
                auto* prop = node->GetProperty<FloatProperty>();
                edge0 = prop ? prop->value : 0.0f;
            }

            // Get edge1 from second property or connected pin
            float edge1 = 1.0f;
            if (node->GetInputs()[1].linkedPinId != -1)
                edge1 = EvaluatePin(node->GetInputs()[1].id, u, v).x;
            else
            {
                // Find second FloatProperty (Edge1)
                int propIdx = 0;
                for (const auto& prop : node->GetProperties())
                {
                    if (auto* fp = dynamic_cast<FloatProperty*>(prop.get()))
                    {
                        if (propIdx == 1)
                        {
                            edge1 = fp->value;
                            break;
                        }
                        propIdx++;
                    }
                }
            }

            // Get X from connected pin or use default 0.5
            float x = 0.5f;
            if (node->GetInputs()[2].linkedPinId != -1)
                x = EvaluatePin(node->GetInputs()[2].id, u, v).x;

            // Compute smoothstep: t = clamp((x - edge0) / (edge1 - edge0), 0, 1)
            float t = (edge1 != edge0) ? (x - edge0) / (edge1 - edge0) : 0.0f;
            t = std::clamp(t, 0.0f, 1.0f);
            float result = t * t * (3.0f - 2.0f * t);
            return Vec4(result, result, result, 1.0f);
        }
        case MaterialNodeType::SplitVec3:
        {
            Vec4 in = EvaluatePin(node->GetInputs()[0].id, u, v);
            if (outputIdx == 0)
                return Vec4(in.x, in.x, in.x, 1.0f);
            if (outputIdx == 1)
                return Vec4(in.y, in.y, in.y, 1.0f);
            if (outputIdx == 2)
                return Vec4(in.z, in.z, in.z, 1.0f);
            break;
        }
        case MaterialNodeType::MakeVec3:
        {
            Vec4 r = EvaluatePin(node->GetInputs()[0].id, u, v);
            Vec4 g = EvaluatePin(node->GetInputs()[1].id, u, v);
            Vec4 b = EvaluatePin(node->GetInputs()[2].id, u, v);
            return Vec4(r.x, g.x, b.x, 1.0f);
        }
        case MaterialNodeType::Texture2D:
        {
            const auto* texNode = static_cast<const Texture2DNode*>(node);
            auto* tilingProp = texNode->GetProperty<TilingProperty>();
            auto* offsetProp = texNode->GetProperty<OffsetProperty>();
            auto* pathProp = texNode->GetProperty<FilePathProperty>();
            float tilingU = tilingProp ? tilingProp->tilingU : 1.0f;
            float tilingV = tilingProp ? tilingProp->tilingV : 1.0f;
            float offsetU = offsetProp ? offsetProp->offsetU : 0.0f;
            float offsetV = offsetProp ? offsetProp->offsetV : 0.0f;
            std::string texPath = pathProp ? pathProp->path : "";

            // UV logic
            float finalU = u * tilingU + offsetU;
            float finalV = v * tilingV + offsetV;

            const MaterialPin& uvPin = texNode->GetInputs()[0];
            int sourcePinId = m_Graph->GetConnectedOutputPin(uvPin.id);
            if (sourcePinId != -1)
            {
                Vec4 uvResult = EvaluatePin(uvPin.id, u, v);
                finalU = uvResult.x;
                finalV = uvResult.y;
            }

            Vec3 texColor = const_cast<MaterialGraphPanel*>(this)->SampleTexture(texPath, finalU, finalV);
            switch (outputIdx)
            {
                case 2: return Vec4(texColor.x, texColor.x, texColor.x, 1.0f);
                case 3: return Vec4(texColor.y, texColor.y, texColor.y, 1.0f);
                case 4: return Vec4(texColor.z, texColor.z, texColor.z, 1.0f);
                case 5: return Vec4(1.0f, 1.0f, 1.0f, 1.0f);
                default: return Vec4(texColor.x, texColor.y, texColor.z, 1.0f);
            }
        }
        case MaterialNodeType::Panner:
        {
            // Recursively evaluate the UV input
            int uvPinId = node->GetInputs()[0].id;
            Vec4 inUV =
                (m_Graph->GetConnectedOutputPin(uvPinId) != -1) ? EvaluatePin(uvPinId, u, v) : Vec4(u, v, 0, 1.0f);
            auto* speedProp = node->GetProperty<PannerSpeedProperty>();
            float su = speedProp ? speedProp->speedU : 0.0f;
            float sv = speedProp ? speedProp->speedV : 0.0f;

            auto* linkProp = node->GetProperty<PannerLinkProperty>();
            if (linkProp && linkProp->value)
                sv = su;

            auto* methodProp = node->GetProperty<PannerMethodProperty>();
            int method = methodProp ? methodProp->value : 0;

            float time = (float)ImGui::GetTime();

            switch (method)
            {
                case 1: // Sine
                    return Vec4(inUV.x + su * sinf(time), inUV.y + sv * sinf(time), 0, 1);
                case 2: // ZigZag
                {
                    float zigzag = abs(fmodf(time * 0.5f, 1.0f) * 2.0f - 1.0f) * 2.0f - 1.0f;
                    return Vec4(inUV.x + su * zigzag, inUV.y + sv * zigzag, 0, 1);
                }
                case 3: // Rotate
                {
                    float angle = time * su;
                    float s = sinf(angle);
                    float c = cosf(angle);
                    float du = inUV.x - 0.5f;
                    float dv = inUV.y - 0.5f;
                    return Vec4(du * c - dv * s + 0.5f, du * s + dv * c + 0.5f, 0, 1);
                }
                case 0: // Linear
                default:
                    return Vec4(inUV.x + time * su, inUV.y + time * sv, 0, 1);
            }
        }
        case MaterialNodeType::Fresnel:
        {
            // Get power from property
            auto* prop = node->GetProperty<FloatProperty>();
            float power = prop ? prop->value : 2.0f;

            // Simple view-based fresnel approximation for preview
            // Using a fake view direction based on UV position
            float viewZ = sqrtf(1.0f - std::clamp((u - 0.5f) * 2.0f, -1.0f, 1.0f) * (u - 0.5f) * 2.0f * (v - 0.5f) *
                                           2.0f * (v - 0.5f) * 2.0f);
            float fresnelVal = powf(1.0f - std::clamp(viewZ, 0.0f, 1.0f), power);
            return Vec4(fresnelVal, fresnelVal, fresnelVal, 1.0f);
        }
        case MaterialNodeType::Time:
        {
            float time = (float)ImGui::GetTime();
            return Vec4(time, time, time, time);
        }
        case MaterialNodeType::TextureCoord:
        {
            return Vec4(u, v, 0, 1.0f);
        }
        case MaterialNodeType::Perlin:
        {
            auto* scaleProp = node->GetProperty<NoiseScaleProperty>();
            auto* seedProp = node->GetProperty<NoiseSeedProperty>();
            auto* octavesProp = node->GetProperty<NoiseOctavesProperty>();
            auto* persistenceProp = node->GetProperty<NoisePersistenceProperty>();
            auto* lacunarityProp = node->GetProperty<NoiseLacunarityProperty>();

            float scale = scaleProp ? scaleProp->value : 5.0f;
            float seed = seedProp ? seedProp->value : 0.0f;
            int octaves = octavesProp ? octavesProp->value : 4;
            float persistence = persistenceProp ? persistenceProp->value : 0.5f;
            float lacunarity = lacunarityProp ? lacunarityProp->value : 2.0f;

            // UV input
            int uvPinId = node->GetInputs()[0].id;
            Vec4 inUV =
                (m_Graph->GetConnectedOutputPin(uvPinId) != -1) ? EvaluatePin(uvPinId, u, v) : Vec4(u, v, 0, 1.0f);

            Vec2 noiseUV(inUV.x * scale + seed, inUV.y * scale + seed);
            float noiseVal = dot_fbm_preview(noiseUV, octaves, persistence, lacunarity);
            noiseVal = noiseVal * 0.5f + 0.5f; // Map to [0, 1]

            return Vec4(noiseVal, noiseVal, noiseVal, 1.0f);
        }
        default:
            break;
    }

    return Vec4(0, 0, 0, 1);
}

std::string MaterialGraphPanel::GetAlbedoTexturePath() const
{
    if (!m_Graph)
        return "";

    // Find PBR Output node
    const MaterialNode* outputNode = nullptr;
    for (const auto& node : m_Graph->GetNodes())
    {
        if (node->GetType() == MaterialNodeType::PBROutput)
        {
            outputNode = node.get();
            break;
        }
    }

    if (!outputNode)
        return "";

    // Check Albedo input
    for (const auto& pin : outputNode->GetInputs())
    {
        if (pin.name != "Albedo" && pin.name != "BaseColor")
            continue;

        if (pin.linkedPinId < 0)
            continue;

        const MaterialNode* sourceNode = m_Graph->GetNodeByPinId(pin.linkedPinId);
        if (!sourceNode)
            continue;

        if (sourceNode->GetType() == MaterialNodeType::Texture2D)
        {
            const auto* texNode = static_cast<const Texture2DNode*>(sourceNode);
            auto* pathProp = texNode->GetProperty<FilePathProperty>();
            return pathProp ? pathProp->path : "";
        }
    }

    return "";
}

const Texture2DNode* MaterialGraphPanel::FindTextureSource(int pinId, int& outChannel) const
{
    if (!m_Graph)
        return nullptr;

    int outputPinId = m_Graph->GetConnectedOutputPin(pinId);
    if (outputPinId == -1)
        return nullptr;

    const MaterialNode* node = m_Graph->GetNodeByPinId(outputPinId);
    if (!node)
        return nullptr;

    if (node->GetType() == MaterialNodeType::Texture2D)
    {
        switch (node->GetOutputIndex(outputPinId))
        {
            case 2: outChannel = 0; break;
            case 3: outChannel = 1; break;
            case 4: outChannel = 2; break;
            default: outChannel = -1; break;
        }
        return static_cast<const Texture2DNode*>(node);
    }

    if (node->GetType() == MaterialNodeType::SplitVec3)
    {
        outChannel = node->GetOutputIndex(outputPinId);
        return FindTextureSource(node->GetInputs()[0].id, outChannel);
    }

    if (node->GetType() == MaterialNodeType::Add || node->GetType() == MaterialNodeType::Multiply ||
        node->GetType() == MaterialNodeType::Lerp)
    {
        // For math nodes, try the first input (arbitrary choice for single texture preview)
        return FindTextureSource(node->GetInputs()[0].id, outChannel);
    }

    return nullptr;
}

const Texture2DNode* MaterialGraphPanel::GetAlbedoTextureNode() const
{
    if (!m_Graph)
        return nullptr;

    // Find PBR Output node
    const MaterialNode* outputNode = nullptr;
    for (const auto& node : m_Graph->GetNodes())
    {
        if (node->GetType() == MaterialNodeType::PBROutput)
        {
            outputNode = node.get();
            break;
        }
    }

    if (!outputNode)
        return nullptr;

    // Check Albedo input
    for (const auto& pin : outputNode->GetInputs())
    {
        if (pin.name != "Albedo" && pin.name != "BaseColor")
            continue;

        if (pin.linkedPinId < 0)
            continue;

        const MaterialNode* sourceNode = m_Graph->GetNodeByPinId(pin.linkedPinId);
        if (!sourceNode)
            continue;

        if (sourceNode->GetType() == MaterialNodeType::Texture2D)
        {
            return static_cast<const Texture2DNode*>(sourceNode);
        }
    }

    return nullptr;
}

void MaterialGraphPanel::DrawPreviewPanel()
{
    if (!m_ShowPreview)
        return;

    ImGui::SetNextWindowSize(ImVec2(200, 250), ImGuiCond_FirstUseEver);
    if (BeginChromeWindow("Material Preview", &m_ShowPreview))
    {
        ImVec2 canvasSize = ImGui::GetContentRegionAvail();
        float previewSize = std::min(canvasSize.x, canvasSize.y - 60);
        if (previewSize < 50)
            previewSize = 50;

        // GPU preview path
        if (m_UseGPUPreview && m_PreviewRenderer && m_PreviewRenderer->IsInitialized())
        {
            // Default albedo from recursive evaluation
            float r = 0.7f, g = 0.7f, b = 0.7f, metallic = 0.0f, roughness = 0.5f;
            EvaluateMaterialOutput(r, g, b, metallic, roughness, 0.5f, 0.5f);

            // Find PBR Output node
            const MaterialNode* outputNode = nullptr;
            for (const auto& node : m_Graph->GetNodes())
            {
                if (node->GetType() == MaterialNodeType::PBROutput)
                {
                    outputNode = node.get();
                    break;
                }
            }

            if (outputNode)
            {
                const std::unordered_map<int, int> effectiveTextureSlots = ResolveEffectiveTextureSlots(m_Graph.get());
                struct PreviewTextureBinding
                {
                    int slot = 0;
                    std::string path;
                    int filterMode = 1;
                    int wrapMode = 0;
                    int sampleType = static_cast<int>(TextureSampleType::Color);
                };

                std::vector<PreviewTextureBinding> previewTextureBindings;
                std::string previewTextureSignature;
                previewTextureBindings.reserve(4);
                for (const auto& node : m_Graph->GetNodes())
                {
                    if (node->GetType() != MaterialNodeType::Texture2D)
                        continue;

                    const auto* texNode = static_cast<const Texture2DNode*>(node.get());
                    const auto* pathProp = texNode->GetProperty<FilePathProperty>();
                    if (!pathProp || pathProp->path.empty())
                        continue;

                    const auto* slotProp = texNode->GetProperty<TextureSlotProperty>();
                    const auto* filterProp = texNode->GetProperty<FilterModeProperty>();
                    const auto* wrapProp = texNode->GetProperty<WrapModeProperty>();
                    const auto* sampleTypeProp = texNode->GetProperty<TextureSampleTypeProperty>();

                    int slot = 0;
                    auto effectiveSlotIt = effectiveTextureSlots.find(texNode->GetId());
                    if (effectiveSlotIt != effectiveTextureSlots.end())
                        slot = effectiveSlotIt->second;
                    else
                    {
                        slot = slotProp ? slotProp->value : 0;
                        if (slot < 0 || slot > 3)
                            slot = 0;
                    }

                    const int filterMode = filterProp ? filterProp->value : 1;
                    const int wrapMode = wrapProp ? wrapProp->value : 0;
                    const int sampleType =
                        sampleTypeProp ? sampleTypeProp->value : static_cast<int>(GuessTextureSampleTypeFromPath(pathProp->path));
                    previewTextureBindings.push_back({slot, pathProp->path, filterMode, wrapMode, sampleType});
                    previewTextureSignature += std::to_string(slot);
                    previewTextureSignature += '|';
                    previewTextureSignature += NormalizeTextureCacheKey(pathProp->path);
                    previewTextureSignature += '|';
                    previewTextureSignature += std::to_string(filterMode);
                    previewTextureSignature += '|';
                    previewTextureSignature += std::to_string(wrapMode);
                    previewTextureSignature += '|';
                    previewTextureSignature += std::to_string(sampleType);
                    previewTextureSignature += ';';
                }

                if (previewTextureSignature != m_LastPreviewTextureSignature)
                {
                    m_PreviewRenderer->ClearTextureSlots();
                    for (const PreviewTextureBinding& binding : previewTextureBindings)
                    {
                        m_PreviewRenderer->SetTextureSlot(binding.slot, binding.path, binding.filterMode, binding.wrapMode,
                                                          binding.sampleType);
                    }
                    m_LastPreviewTextureSignature = previewTextureSignature;
                }

                // Keep the legacy UV controls alive for simple slot-0 preview fallback.
                int albedoChannel = -1;
                const Texture2DNode* albedoTexNode = nullptr;
                for (const auto& pin : outputNode->GetInputs())
                {
                    if (pin.name != "Albedo" && pin.name != "BaseColor")
                        continue;
                    albedoTexNode = FindTextureSource(pin.id, albedoChannel);
                    if (albedoTexNode)
                        break;
                }

                if (albedoTexNode)
                {
                    auto* pathProp = albedoTexNode->GetProperty<FilePathProperty>();
                    auto* tilingProp = albedoTexNode->GetProperty<TilingProperty>();
                    auto* offsetProp = albedoTexNode->GetProperty<OffsetProperty>();
                    auto* filterProp = albedoTexNode->GetProperty<FilterModeProperty>();
                    auto* wrapProp = albedoTexNode->GetProperty<WrapModeProperty>();

                    std::string path = pathProp ? pathProp->path : "";
                    float tu = tilingProp ? tilingProp->tilingU : 1.0f;
                    float tv = tilingProp ? tilingProp->tilingV : 1.0f;
                    float ou = offsetProp ? offsetProp->offsetU : 0.0f;
                    float ov = offsetProp ? offsetProp->offsetV : 0.0f;
                    int filterMode = filterProp ? filterProp->value : 1;
                    int wrapMode = wrapProp ? wrapProp->value : 0;

                    float psu = 0.0f;
                    float psv = 0.0f;
                    const MaterialPin& uvPin = albedoTexNode->GetInputs()[0];
                    int sourcePinId = m_Graph->GetConnectedOutputPin(uvPin.id);
                    if (sourcePinId != -1)
                    {
                        const MaterialNode* sourceNode = m_Graph->GetNodeByPinId(sourcePinId);
                        if (sourceNode && sourceNode->GetType() == MaterialNodeType::Panner)
                        {
                            auto* speedProp = sourceNode->GetProperty<PannerSpeedProperty>();
                            if (speedProp)
                            {
                                psu = speedProp->speedU;
                                psv = speedProp->speedV;
                            }
                        }
                    }
                    m_PreviewRenderer->SetAlbedoTexture(path, tu, tv, ou, ov, filterMode, wrapMode, psu, psv, albedoChannel);
                }

                // Update custom shader if HLSL changed
                std::string currentHLSL = m_Graph->GenerateHLSL();
                if (currentHLSL != m_LastGeneratedHLSL)
                {
                    m_PreviewRenderer->UpdateCustomMaterial(currentHLSL);
                    m_LastGeneratedHLSL = currentHLSL;
                }

                // Render to the offscreen texture
                m_PreviewRenderer->Render(r, g, b, metallic, roughness, m_PreviewRotation);
            }

            // Display the GPU-rendered texture
            void* texId = m_PreviewRenderer->GetTextureId();
            if (texId)
            {
                float gpuPreviewSize = std::min(previewSize, static_cast<float>(m_PreviewRenderer->GetSize()));
                ImGui::Image(texId, ImVec2(gpuPreviewSize, gpuPreviewSize));

                // Rotation slider
                ImGui::SetNextItemWidth(gpuPreviewSize);
                ImGui::SliderFloat("##Rotation", &m_PreviewRotation, 0.0f, 360.0f, "Rotation: %.0f");

                // Material info
                ImGui::Text("Albedo: %.2f, %.2f, %.2f", r, g, b);
                ImGui::Text("Metallic: %.2f  Roughness: %.2f", metallic, roughness);
                ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1.0f), "[GPU Preview]");

                ImGui::End();
                return;
            }
        }

        // CPU fallback preview
        ImVec2 canvasPos = ImGui::GetCursorScreenPos();
        ImVec2 center(canvasPos.x + previewSize * 0.5f, canvasPos.y + previewSize * 0.5f);
        float sphereRadius = previewSize * 0.4f;

        ImDrawList* drawList = ImGui::GetWindowDrawList();

        // Background
        drawList->AddRectFilled(canvasPos, ImVec2(canvasPos.x + previewSize, canvasPos.y + previewSize),
                                IM_COL32(30, 30, 35, 255));

        // Draw lit sphere preview
        // Simulate PBR lighting with simple diffuse + specular
        const int segments = 64;
        const float lightDirX = 0.5f, lightDirY = -0.6f, lightDirZ = -0.6f;
        float lightLen = sqrtf(lightDirX * lightDirX + lightDirY * lightDirY + lightDirZ * lightDirZ);
        float ldx = lightDirX / lightLen, ldy = lightDirY / lightLen, ldz = lightDirZ / lightLen;

        // Apply rotation to light direction
        float rotRad = m_PreviewRotation * 3.14159f / 180.0f;
        float cosR = cosf(rotRad), sinR = sinf(rotRad);
        float rotLdx = ldx * cosR - ldz * sinR;
        float rotLdz = ldx * sinR + ldz * cosR;
        ldx = rotLdx;
        ldz = rotLdz;

        // Draw sphere using filled triangles with per-vertex lighting
        for (int lat = 0; lat < segments; ++lat)
        {
            float theta1 = 3.14159f * lat / segments;
            float theta2 = 3.14159f * (lat + 1) / segments;

            for (int lon = 0; lon < segments; ++lon)
            {
                float phi1 = 2.0f * 3.14159f * lon / segments;
                float phi2 = 2.0f * 3.14159f * (lon + 1) / segments;

                // Calculate sphere position for center of quad (for lighting)
                float midTheta = (theta1 + theta2) * 0.5f;
                float midPhi = (phi1 + phi2) * 0.5f;
                float nx = sinf(midTheta) * cosf(midPhi);
                float ny = -cosf(midTheta);
                float nz = sinf(midTheta) * sinf(midPhi);

                // Calculate UVs for texture sampling
                float u = midPhi / (2.0f * 3.14159f);
                float v = midTheta / 3.14159f;

                // Evaluate material at this point
                float r, g, b, metallic, roughness;
                EvaluateMaterialOutput(r, g, b, metallic, roughness, u, v);

                // Simple diffuse lighting
                float ndotl = std::max(0.0f, -(nx * ldx + ny * ldy + nz * ldz));

                // Add fresnel-like rim lighting for metallic effect
                float fresnel = powf(1.0f - fabsf(nz), 2.0f + metallic * 3.0f);

                // Combine for final color
                float ambient = 0.15f + roughness * 0.1f;
                float diffuse = ndotl * (1.0f - metallic * 0.5f);
                float specular = powf(ndotl, 16.0f / (roughness + 0.1f)) * (1.0f - roughness) * 0.5f;
                float rimLight = fresnel * metallic * 0.3f;

                float intensity = ambient + diffuse + specular + rimLight;

                int finalR = static_cast<int>(std::clamp(r * intensity * 255.0f, 0.0f, 255.0f));
                int finalG = static_cast<int>(std::clamp(g * intensity * 255.0f, 0.0f, 255.0f));
                int finalB = static_cast<int>(std::clamp(b * intensity * 255.0f, 0.0f, 255.0f));

                ImU32 quadColor = IM_COL32(finalR, finalG, finalB, 255);

                // Only draw front-facing quads
                if (nz < 0)
                {
                    // Project quad corners to 2D
                    float x1 = center.x + sphereRadius * sinf(theta1) * cosf(phi1);
                    float y1 = center.y + sphereRadius * -cosf(theta1);
                    float x2 = center.x + sphereRadius * sinf(theta1) * cosf(phi2);
                    float y2 = center.y + sphereRadius * -cosf(theta1);
                    float x3 = center.x + sphereRadius * sinf(theta2) * cosf(phi2);
                    float y3 = center.y + sphereRadius * -cosf(theta2);
                    float x4 = center.x + sphereRadius * sinf(theta2) * cosf(phi1);
                    float y4 = center.y + sphereRadius * -cosf(theta2);

                    drawList->AddQuadFilled(ImVec2(x1, y1), ImVec2(x2, y2), ImVec2(x3, y3), ImVec2(x4, y4), quadColor);
                }
            }
        }

        // Reserve space
        ImGui::Dummy(ImVec2(previewSize, previewSize));

        // Rotation slider
        ImGui::SliderFloat("Rotation", &m_PreviewRotation, 0.0f, 360.0f);

        // Material info - evaluate once for display
        float dispR, dispG, dispB, dispMetal, dispRough;
        EvaluateMaterialOutput(dispR, dispG, dispB, dispMetal, dispRough, 0.5f, 0.5f);

        ImGui::Separator();
        ImGui::TextColored(ImVec4(dispR, dispG, dispB, 1.0f), "Albedo");
        ImGui::SameLine();
        ImGui::Text("(%.2f, %.2f, %.2f)", dispR, dispG, dispB);
        ImGui::Text("Metallic: %.2f", dispMetal);
        ImGui::Text("Roughness: %.2f", dispRough);
    }
    ImGui::End();
}

// =============================================================================
// Undo/Redo System
// =============================================================================

void MaterialGraphPanel::PushUndoCommand(const GraphCommand& cmd)
{
    m_UndoStack.push_back(cmd);
    if (m_UndoStack.size() > kMaxUndoHistory)
        m_UndoStack.erase(m_UndoStack.begin());
    ClearRedoStack();
}

void MaterialGraphPanel::ClearRedoStack()
{
    m_RedoStack.clear();
}

void MaterialGraphPanel::Undo()
{
    if (m_UndoStack.empty() || !m_Graph)
        return;

    GraphCommand cmd = m_UndoStack.back();
    m_UndoStack.pop_back();

    switch (cmd.type)
    {
        case GraphCommandType::AddNode:
            // Undo add = delete
            m_Graph->RemoveNode(cmd.nodeId);
            if (m_SelectedNodeId == cmd.nodeId)
                m_SelectedNodeId = -1;
            break;

        case GraphCommandType::DeleteNode:
            // Undo delete = re-add (limited - can't fully restore)
            // For now just show message - full restore would need node serialization
            break;

        case GraphCommandType::MoveNode:
            // Undo move = restore old position
            for (auto& node : m_Graph->GetNodes())
            {
                if (node->GetId() == cmd.nodeId)
                {
                    node->posX = cmd.oldPosX;
                    node->posY = cmd.oldPosY;
                    break;
                }
            }
            break;

        case GraphCommandType::AddConnection:
            // Undo add connection = disconnect
            m_Graph->Disconnect(cmd.connectionId);
            break;

        case GraphCommandType::DeleteConnection:
            // Undo delete = reconnect
            m_Graph->Connect(cmd.outputPinId, cmd.inputPinId);
            break;

        default:
            break;
    }

    m_RedoStack.push_back(cmd);
}

void MaterialGraphPanel::Redo()
{
    if (m_RedoStack.empty() || !m_Graph)
        return;

    GraphCommand cmd = m_RedoStack.back();
    m_RedoStack.pop_back();

    switch (cmd.type)
    {
        case GraphCommandType::AddNode:
            // Redo add = add again (can't fully restore, just push to undo)
            break;

        case GraphCommandType::DeleteNode:
            // Redo delete = delete again
            m_Graph->RemoveNode(cmd.nodeId);
            if (m_SelectedNodeId == cmd.nodeId)
                m_SelectedNodeId = -1;
            break;

        case GraphCommandType::MoveNode:
            // Redo move = move to new position
            for (auto& node : m_Graph->GetNodes())
            {
                if (node->GetId() == cmd.nodeId)
                {
                    node->posX = cmd.posX;
                    node->posY = cmd.posY;
                    break;
                }
            }
            break;

        case GraphCommandType::AddConnection:
            // Redo add connection = reconnect
            m_Graph->Connect(cmd.outputPinId, cmd.inputPinId);
            break;

        case GraphCommandType::DeleteConnection:
            // Redo delete = disconnect again
            m_Graph->Disconnect(cmd.connectionId);
            break;

        default:
            break;
    }

    m_UndoStack.push_back(cmd);
}

// =============================================================================
// Copy/Paste System
// =============================================================================

void MaterialGraphPanel::CopySelectedNodes()
{
    m_Clipboard.nodes.clear();
    m_Clipboard.hasData = false;

    if (!m_Graph)
        return;

    // Use selected nodes if any, otherwise use single selected node
    std::vector<int> nodesToCopy;
    if (!m_SelectedNodeIds.empty())
        nodesToCopy = m_SelectedNodeIds;
    else if (m_SelectedNodeId >= 0)
        nodesToCopy.push_back(m_SelectedNodeId);

    if (nodesToCopy.empty())
        return;

    // Find reference position (first node)
    float refX = 0, refY = 0;
    bool foundRef = false;

    for (const auto& node : m_Graph->GetNodes())
    {
        if (std::find(nodesToCopy.begin(), nodesToCopy.end(), node->GetId()) != nodesToCopy.end())
        {
            if (!foundRef)
            {
                refX = node->posX;
                refY = node->posY;
                foundRef = true;
            }

            NodeClipboard::NodeData data;
            data.type = node->GetType();
            data.relX = node->posX - refX;
            data.relY = node->posY - refY;
            data.value = 0;
            data.valueX = data.valueY = data.valueZ = 0;

            // Store node-specific values via property system
            if (data.type == MaterialNodeType::ConstFloat)
            {
                auto* floatNode = static_cast<ConstFloatNode*>(node.get());
                auto* prop = floatNode->GetProperty<FloatProperty>();
                data.value = prop ? prop->value : 0.0f;
            }
            else if (data.type == MaterialNodeType::ConstVec3)
            {
                auto* vec3Node = static_cast<ConstVec3Node*>(node.get());
                auto* prop = vec3Node->GetProperty<Vec3Property>();
                Vec3 val = prop ? prop->value : Vec3(1, 1, 1);
                data.valueX = val.x;
                data.valueY = val.y;
                data.valueZ = val.z;
            }
            else if (data.type == MaterialNodeType::Texture2D)
            {
                auto* texNode = static_cast<Texture2DNode*>(node.get());
                auto* pathProp = texNode->GetProperty<FilePathProperty>();
                data.texturePath = pathProp ? pathProp->path : "";
            }

            m_Clipboard.nodes.push_back(data);
        }
    }

    m_Clipboard.hasData = !m_Clipboard.nodes.empty();
}

void MaterialGraphPanel::PasteNodes()
{
    if (!m_Clipboard.hasData || !m_Graph)
        return;

    // Paste at current scroll position with offset
    float pasteX = -m_ScrollOffset.x + 100;
    float pasteY = -m_ScrollOffset.y + 100;

    m_SelectedNodeIds.clear();

    for (const auto& data : m_Clipboard.nodes)
    {
        // Don't paste PBR Output nodes
        if (data.type == MaterialNodeType::PBROutput)
            continue;

        MaterialNode* newNode = m_Graph->AddNode(data.type);
        if (!newNode)
            continue;

        // Set position
        newNode->posX = pasteX + data.relX;
        newNode->posY = pasteY + data.relY;
        int nodeId = newNode->GetId();

        // Restore node-specific values
        if (data.type == MaterialNodeType::ConstFloat)
        {
            auto* floatNode = static_cast<ConstFloatNode*>(newNode);
            auto* prop = floatNode->GetProperty<FloatProperty>();
            if (prop)
                prop->value = data.value;
        }
        else if (data.type == MaterialNodeType::ConstVec3)
        {
            auto* vec3Node = static_cast<ConstVec3Node*>(newNode);
            auto* prop = vec3Node->GetProperty<Vec3Property>();
            if (prop)
                prop->value = Vec3(data.valueX, data.valueY, data.valueZ);
        }
        else if (data.type == MaterialNodeType::Texture2D)
        {
            auto* texNode = static_cast<Texture2DNode*>(newNode);
            auto* pathProp = texNode->GetProperty<FilePathProperty>();
            if (pathProp)
                pathProp->path = data.texturePath;
        }

        m_SelectedNodeIds.push_back(nodeId);
    }
}

// =============================================================================
// Snap to Grid
// =============================================================================

float MaterialGraphPanel::SnapToGrid(float value)
{
    return std::round(value / m_GridSnapSize) * m_GridSnapSize;
}

void MaterialGraphPanel::SnapSelectedNodes()
{
    if (!m_Graph)
        return;

    std::vector<int> nodesToSnap;
    if (!m_SelectedNodeIds.empty())
        nodesToSnap = m_SelectedNodeIds;
    else if (m_SelectedNodeId >= 0)
        nodesToSnap.push_back(m_SelectedNodeId);

    for (auto& node : m_Graph->GetNodes())
    {
        if (std::find(nodesToSnap.begin(), nodesToSnap.end(), node->GetId()) != nodesToSnap.end())
        {
            node->posX = SnapToGrid(node->posX);
            node->posY = SnapToGrid(node->posY);
        }
    }
}

// =============================================================================
// Keyboard Shortcuts
// =============================================================================

void MaterialGraphPanel::HandleKeyboardShortcuts()
{
    ImGuiIO& io = ImGui::GetIO();

    // Only handle if not typing in a text field
    if (io.WantTextInput)
        return;

    bool ctrl = io.KeyCtrl;

    // Ctrl+Z - Undo
    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_Z) && !io.KeyShift)
    {
        Undo();
    }

    // Ctrl+Y or Ctrl+Shift+Z - Redo
    if ((ctrl && ImGui::IsKeyPressed(ImGuiKey_Y)) || (ctrl && io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_Z)))
    {
        Redo();
    }

    // Ctrl+C - Copy
    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_C))
    {
        CopySelectedNodes();
    }

    // Ctrl+V - Paste
    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_V))
    {
        PasteNodes();
    }

    // Delete key - Delete selected nodes
    if (ImGui::IsKeyPressed(ImGuiKey_Delete))
    {
        if (m_SelectedNodeId >= 0)
        {
            // Track for undo
            GraphCommand cmd;
            cmd.type = GraphCommandType::DeleteNode;
            cmd.nodeId = m_SelectedNodeId;
            PushUndoCommand(cmd);

            m_Graph->RemoveNode(m_SelectedNodeId);
            m_SelectedNodeId = -1;
        }

        for (int nodeId : m_SelectedNodeIds)
        {
            GraphCommand cmd;
            cmd.type = GraphCommandType::DeleteNode;
            cmd.nodeId = nodeId;
            PushUndoCommand(cmd);

            m_Graph->RemoveNode(nodeId);
        }
        m_SelectedNodeIds.clear();
    }

    // G key - Toggle snap to grid
    if (ImGui::IsKeyPressed(ImGuiKey_G) && !ctrl)
    {
        m_SnapToGrid = !m_SnapToGrid;
    }

    // Ctrl+G - Snap selected nodes to grid
    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_G))
    {
        SnapSelectedNodes();
    }
}

} // namespace Dot
