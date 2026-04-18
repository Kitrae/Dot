// =============================================================================
// Dot Engine - Material Graph Editor Panel
// =============================================================================
// Visual node-based editor for creating materials using custom ImGui canvas.
// =============================================================================

#pragma once

#include "Core/Material/MaterialGraph.h"
#include "Core/Math/Vec3.h"
#include "Core/Math/Vec4.h"

#include "EditorPanel.h"

#include <imgui.h>
#include <memory>
#include <stack>
#include <string>
#include <unordered_map>
#include <vector>

namespace Dot
{

// Forward declarations for GPU preview
class RHIDevice;
class RHIGUI;
class RHITexture;
using RHITexturePtr = std::shared_ptr<RHITexture>;
class MaterialPreviewRenderer;

// Graph editing tools
enum class GraphTool
{
    Select,    // Default: select, drag nodes, create connections
    Knife,     // Cut connections by dragging across them
    BoxSelect, // Drag rectangle to select multiple nodes
};

// Command types for undo/redo
enum class GraphCommandType
{
    AddNode,
    DeleteNode,
    MoveNode,
    AddConnection,
    DeleteConnection,
    PasteNodes,
};

// Command for undo/redo
struct GraphCommand
{
    GraphCommandType type;

    // Node data
    int nodeId = -1;
    MaterialNodeType nodeType = MaterialNodeType::ConstFloat;
    float posX = 0, posY = 0;
    float oldPosX = 0, oldPosY = 0;           // For move commands
    float value = 0;                          // For ConstFloat
    float valueX = 0, valueY = 0, valueZ = 0; // For ConstVec3
    std::string texturePath;                  // For Texture2D

    // Connection data
    int connectionId = -1;
    int outputPinId = -1;
    int inputPinId = -1;

    // For paste: multiple nodes
    std::vector<int> nodeIds;
};

// Clipboard data for copy/paste
struct NodeClipboard
{
    struct NodeData
    {
        MaterialNodeType type;
        float relX, relY; // Relative to first node
        float value;
        float valueX, valueY, valueZ;
        std::string texturePath;
    };
    std::vector<NodeData> nodes;
    bool hasData = false;
};

class MaterialGraphPanel : public EditorPanel
{
public:
    MaterialGraphPanel();
    ~MaterialGraphPanel() override;

    void OnImGui() override;

    // Create a new empty material
    void NewMaterial();

    // Load material from file
    void LoadMaterial(const std::string& path);

    // Save current material
    void SaveMaterial(const std::string& path);

    // Get generated HLSL code
    std::string GetGeneratedCode() const;

    // GPU preview initialization
    void SetDevice(RHIDevice* device) { m_Device = device; }
    void SetGUI(RHIGUI* gui) { m_GUI = gui; }
    bool InitializePreview();

private:
    // Main drawing functions
    void DrawToolbar();
    void DrawToolButtons(); // Tool selection buttons
    void DrawNodePalette();
    void DrawNodeCanvas();
    void DrawPropertyPanel();
    void DrawCodePreview();

    // Node canvas drawing
    void DrawGrid(ImDrawList* drawList, const ImVec2& canvasPos, const ImVec2& canvasSize);
    void DrawNodes(ImDrawList* drawList, const ImVec2& canvasPos, const ImVec2& canvasSize);
    void DrawPin(ImDrawList* drawList, const ImVec2& pos, const MaterialPin& pin, const ImVec2& mousePos,
                 float pinRadius);
    void DrawConnections(ImDrawList* drawList, const ImVec2& canvasPos);
    void DrawBezierConnection(ImDrawList* drawList, const ImVec2& start, const ImVec2& end, ImU32 color);

    // Tool drawing
    void DrawKnifeTrail(ImDrawList* drawList);
    void DrawBoxSelection(ImDrawList* drawList);

    // Tool logic
    void HandleKnifeTool(bool canvasHovered, const ImVec2& canvasPos, ImDrawList* drawList);
    void HandleBoxSelectTool(bool canvasHovered, const ImVec2& canvasPos);

    // Undo/Redo
    void Undo();
    void Redo();
    void PushUndoCommand(const GraphCommand& cmd);
    void ClearRedoStack();

    // Copy/Paste
    void CopySelectedNodes();
    void PasteNodes();

    // Snap to grid
    float SnapToGrid(float value);
    void SnapSelectedNodes();

    // Keyboard handling
    void HandleKeyboardShortcuts();

    // Helper functions
    ImVec2 GetPinScreenPosition(int pinId, const ImVec2& canvasPos);
    bool LineIntersectsBezier(const ImVec2& lineStart, const ImVec2& lineEnd, const ImVec2& bezierStart,
                              const ImVec2& bezierEnd);

    // Legacy compatibility
    void DrawNodeEditor();
    void HandleNodeCreation();
    void HandleConnections();

    // Graph data
    std::unique_ptr<MaterialGraph> m_Graph;

    // Selection state
    int m_SelectedNodeId = -1;
    int m_DraggingNode = -1;
    int m_HoveredPin = -1;
    std::vector<int> m_SelectedNodeIds; // Multi-select support

    // Connection creation
    bool m_CreatingConnection = false;
    int m_ConnectionStartPin = -1;
    bool m_ConnectionStartIsInput = false;

    // Canvas state
    ImVec2 m_ScrollOffset;
    float m_Zoom = 1.0f;
    ImVec2 m_CanvasPos; // Store for tool usage

    // UI state
    bool m_ShowCodePreview = false;
    bool m_ShowPalette = true;

    // Node creation popup
    bool m_OpenNodePopup = false;
    float m_PopupPosX = 0.0f;
    float m_PopupPosY = 0.0f;
    char m_NodeSearchQuery[64] = {0};
    bool m_FocusSearch = false;

    // Tool system
    GraphTool m_CurrentTool = GraphTool::Select;

    // Knife tool state
    bool m_KnifeActive = false;
    std::vector<ImVec2> m_KnifeTrail;

    // Box select state
    bool m_BoxSelectActive = false;
    ImVec2 m_BoxSelectStart;
    ImVec2 m_BoxSelectEnd;

    // Material preview state
    bool m_ShowPreview = true;
    float m_PreviewRotation = 0.0f;

    // Undo/Redo stacks
    std::vector<GraphCommand> m_UndoStack;
    std::vector<GraphCommand> m_RedoStack;
    static constexpr int kMaxUndoHistory = 50;

    // Node dragging for undo
    bool m_WasDragging = false;
    float m_DragStartX = 0, m_DragStartY = 0;

    // Clipboard
    NodeClipboard m_Clipboard;

    // Snap to grid
    bool m_SnapToGrid = false;
    float m_GridSnapSize = 20.0f;

    // Preview
    void DrawPreviewPanel();
    void EvaluateMaterialOutput(float& r, float& g, float& b, float& metallic, float& roughness, float u, float v);
    Vec4 EvaluatePin(int pinId, float u, float v);
    std::string GetAlbedoTexturePath() const;          // Get texture path for GPU preview
    const Texture2DNode* GetAlbedoTextureNode() const; // Get full texture node
    const Texture2DNode* FindTextureSource(int pinId, int& outChannel) const;

    // Texture cache for CPU sampling
    struct TextureCacheEntry
    {
        int width = 0;
        int height = 0;
        int channels = 0;
        unsigned char* data = nullptr;
    };
    std::unordered_map<std::string, TextureCacheEntry> m_TextureCache;
    Vec3 SampleTexture(const std::string& path, float u, float v);
    void ClearTextureCache();

    // GPU Thumbnail Cache
    struct GPUThumbnail
    {
        RHITexturePtr resource;
        void* imGuiTexId = nullptr;
    };
    std::unordered_map<std::string, GPUThumbnail> m_GPUThumbnails;
    void* GetGPUThumbnail(const std::string& path);

    // GPU preview
    RHIDevice* m_Device = nullptr;
    RHIGUI* m_GUI = nullptr;
    std::unique_ptr<MaterialPreviewRenderer> m_PreviewRenderer;
    bool m_UseGPUPreview = false;
    std::string m_LastGeneratedHLSL;
    std::string m_LastPreviewTextureSignature;
    int m_ThumbnailLoadsThisFrame = 0;
};

} // namespace Dot
