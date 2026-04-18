// =============================================================================
// Dot Engine - UI Workspace
// =============================================================================
// Editor workspace for authoring .dotui assets.
// =============================================================================

#pragma once

#include "Workspace.h"

#include <filesystem>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <imgui.h>

namespace Dot
{

class UIAssetDocument;
struct UIWidgetNode;

class UIWorkspace : public Workspace
{
public:
    UIWorkspace();

    void OnActivate() override;
    void OnDeactivate() override;
    void OnImGui() override;

    bool OpenAsset(const std::filesystem::path& path);
    bool NewAsset();
    bool SaveAsset();
    bool SaveAssetAs();
    const std::filesystem::path& GetAssetPath() const;
    bool HasOpenAsset() const;
    bool IsAssetDirty() const;

private:
    enum class CanvasResizeHandle
    {
        None = 0,
        TopLeft,
        TopRight,
        BottomLeft,
        BottomRight
    };

    struct CanvasDrawItem
    {
        const UIWidgetNode* node = nullptr;
        ImVec2 min{};
        ImVec2 max{};
        int depth = 0;
        int order = 0;
    };

    struct NodeCacheEntry
    {
        const UIWidgetNode* node = nullptr;
        const UIWidgetNode* parent = nullptr;
        std::string path;
        std::string searchText;
    };

    void HandleKeyboardShortcuts();
    void DrawMenuBar();
    void DrawHierarchyPanel();
    void DrawCanvasPanel();
    void DrawInspectorPanel();
    void DrawCreateQuickActions();
    void DrawHierarchySearchBar();
    void RebuildHierarchyCache(const std::string& filterLower);
    bool CollectHierarchyMatches(const UIWidgetNode& node, const std::string& filterLower, bool filterActive, int& visibleCount);
    void EnsureNodeCache() const;
    void BuildNodeCacheRecursive(const UIWidgetNode& node, const UIWidgetNode* parent, const std::string& path) const;
    void EnsureDocumentSuggestionCache();
    void InvalidateAssetOptionCachesIfNeeded();
    const std::vector<std::string>& GetFontAssetOptions();
    const std::vector<std::string>& GetImageAssetOptions();
    const std::vector<std::string>& GetMaterialAssetOptions();

    void DrawWidgetTree(UIWidgetNode& node, const std::string* filterLower = nullptr);
    void CollectCanvasDrawItems(const ImVec2& canvasStart, const ImVec2& canvasEnd, std::vector<CanvasDrawItem>& items) const;
    void EnsureCanvasDrawCache(const ImVec2& canvasStart, const ImVec2& canvasEnd) const;
    void DrawCanvasNodeRecursive(const UIWidgetNode& node, const ImVec2& parentMin, const ImVec2& parentMax,
                                 std::vector<CanvasDrawItem>& items, int depth, int& order) const;
    void DrawInspectorForSelectedNode();

    UIWidgetNode* FindSelectedNode();
    const UIWidgetNode* FindSelectedNode() const;
    UIWidgetNode* FindParentOfNode(const std::string& id);
    const UIWidgetNode* FindParentOfNode(const std::string& id) const;
    UIWidgetNode* FindNodeById(const std::string& id);
    const UIWidgetNode* FindNodeById(const std::string& id) const;
    UIWidgetNode* FindNodeByIdRecursive(UIWidgetNode& node, const std::string& id);
    const UIWidgetNode* FindNodeByIdRecursive(const UIWidgetNode& node, const std::string& id) const;
    UIWidgetNode* FindParentOfNodeRecursive(UIWidgetNode& node, const std::string& id);
    const UIWidgetNode* FindParentOfNodeRecursive(const UIWidgetNode& node, const std::string& id) const;
    bool RemoveNodeById(UIWidgetNode& node, const std::string& id);
    std::string MakeUniqueNodeId(const std::string& base) const;
    UIWidgetNode MakeNodeTemplate(const char* label) const;
    void AddNodeToSelection(UIWidgetNode node);
    void AddNodeAsSiblingToSelection(UIWidgetNode node);
    void SelectNode(const std::string& id);
    void SelectParentNode();
    void DeleteSelectedNode();
    void DuplicateSelectedNode();
    bool ExtractNodeById(UIWidgetNode& node, const std::string& id, UIWidgetNode& outNode);
    bool InsertNodeAsChild(const std::string& parentId, UIWidgetNode node);
    bool InsertNodeAfter(const std::string& targetId, UIWidgetNode node);
    bool NodeSubtreeContainsId(const UIWidgetNode& node, const std::string& id) const;
    bool MoveNodeToParent(const std::string& nodeId, const std::string& parentId);
    bool MoveNodeAfter(const std::string& nodeId, const std::string& targetId);

    static ImVec2 ComputeNodeMin(const UIWidgetNode& node, const ImVec2& parentMin, const ImVec2& parentMax);
    static ImVec2 ComputeNodeMax(const UIWidgetNode& node, const ImVec2& parentMin, const ImVec2& parentMax);
    bool NodeMatchesHierarchyFilter(const UIWidgetNode& node, const std::string& filterLower) const;
    bool BuildNodePathRecursive(const UIWidgetNode& node, const std::string& targetId, std::vector<std::string>& path) const;
    bool IsNodeVisibleInHierarchy(const UIWidgetNode& node, const std::string* filterLower) const;
    std::string GetSelectedNodePath() const;

    std::unique_ptr<UIAssetDocument> m_Document;
    std::string m_SelectedNodeId;
    float m_HierarchyWidth = 250.0f;
    float m_InspectorWidth = 320.0f;
    std::string m_StatusMessage;
    bool m_StatusIsError = false;
    bool m_FocusHierarchySearch = false;
    bool m_CanvasDragging = false;
    bool m_CanvasResizing = false;
    bool m_CanvasInteractionDirtyPending = false;
    std::string m_CanvasDragNodeId;
    ImVec2 m_CanvasDragMouseStart{};
    float m_CanvasDragOffsetX = 0.0f;
    float m_CanvasDragOffsetY = 0.0f;
    float m_CanvasResizeStartOffsetX = 0.0f;
    float m_CanvasResizeStartOffsetY = 0.0f;
    float m_CanvasResizeStartSizeX = 0.0f;
    float m_CanvasResizeStartSizeY = 0.0f;
    CanvasResizeHandle m_CanvasResizeHandle = CanvasResizeHandle::None;
    char m_HierarchySearchBuffer[128]{};
    uint64_t m_HierarchyCacheRevision = 0;
    std::string m_HierarchyCacheFilter;
    std::unordered_set<std::string> m_HierarchyVisibleNodeIds;
    int m_HierarchyVisibleCount = 0;
    mutable uint64_t m_NodeCacheRevision = 0;
    mutable std::unordered_map<std::string, NodeCacheEntry> m_NodeCache;
    mutable uint64_t m_SelectedPathCacheRevision = 0;
    mutable std::string m_SelectedPathCacheNodeId;
    mutable std::string m_SelectedPathCacheValue;
    mutable uint64_t m_CanvasDrawCacheRevision = 0;
    mutable ImVec2 m_CanvasDrawCacheStart{};
    mutable ImVec2 m_CanvasDrawCacheEnd{};
    mutable std::vector<CanvasDrawItem> m_CanvasDrawCache;
    uint64_t m_DocumentSuggestionCacheRevision = 0;
    std::vector<std::string> m_BindingSuggestionOptions;
    std::vector<std::string> m_EventSuggestionOptions;
    std::string m_AssetOptionCacheRoot;
    std::vector<std::string> m_FontAssetOptions;
    std::vector<std::string> m_ImageAssetOptions;
    std::vector<std::string> m_MaterialAssetOptions;
};

} // namespace Dot



