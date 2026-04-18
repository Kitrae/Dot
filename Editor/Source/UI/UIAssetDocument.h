// =============================================================================
// Dot Engine - UI Asset Document
// =============================================================================
// Editor-side .dotui document model and serializer-facing data.
// =============================================================================

#pragma once

#include "Core/Core.h"

#include <filesystem>
#include <string>
#include <unordered_set>
#include <vector>

namespace Dot
{

enum class UIWidgetType : uint8
{
    Panel = 0,
    Text,
    Image,
    Button,
    ProgressBar,
    Spacer
};

struct UILayout
{
    float anchorMinX = 0.0f;
    float anchorMinY = 0.0f;
    float anchorMaxX = 1.0f;
    float anchorMaxY = 1.0f;
    float offsetX = 0.0f;
    float offsetY = 0.0f;
    float sizeX = 200.0f;
    float sizeY = 48.0f;
    float alignX = 0.0f;
    float alignY = 0.0f;
    int zOrder = 0;
    bool visible = true;
};

struct UIStyle
{
    float backgroundR = 0.16f;
    float backgroundG = 0.19f;
    float backgroundB = 0.24f;
    float backgroundA = 1.0f;
    float textR = 0.94f;
    float textG = 0.95f;
    float textB = 0.98f;
    float textA = 1.0f;
    float borderR = 0.26f;
    float borderG = 0.31f;
    float borderB = 0.38f;
    float borderA = 1.0f;
    float borderThickness = 1.0f;
    float paddingX = 10.0f;
    float paddingY = 8.0f;
    float opacity = 1.0f;
    std::string fontPath;
    std::string imagePath;
    std::string materialPath;
};

struct UIWidgetNode
{
    std::string id = "Root";
    std::string name = "Root";
    UIWidgetType type = UIWidgetType::Panel;
    std::string text;
    std::string bindingKey;
    std::string onClickEvent;
    std::string onChangeEvent;
    float progress = 0.5f;
    bool enabled = true;
    UILayout layout;
    UIStyle style;
    std::vector<UIWidgetNode> children;
};

struct UIAsset
{
    int version = 1;
    std::string name = "UI";
    UIWidgetNode root;
};

class UIAssetDocument
{
public:
    UIAssetDocument();

    void New();
    bool Load(const std::filesystem::path& path);
    bool Save();
    bool SaveAs(const std::filesystem::path& path);
    void SetPath(const std::filesystem::path& path) { m_Path = path; }

    const std::filesystem::path& GetPath() const { return m_Path; }
    bool HasPath() const { return !m_Path.empty(); }
    bool IsDirty() const { return m_Dirty; }
    void SetDirty(bool dirty) { m_Dirty = dirty; }
    const std::string& GetLastError() const { return m_LastError; }
    uint64 GetRevision() const { return m_Revision; }

    UIAsset& GetAsset() { return m_Asset; }
    const UIAsset& GetAsset() const { return m_Asset; }
    UIWidgetNode& GetRoot() { return m_Asset.root; }
    const UIWidgetNode& GetRoot() const { return m_Asset.root; }

    UIWidgetNode* FindWidget(const std::string& id);
    const UIWidgetNode* FindWidget(const std::string& id) const;
    UIWidgetNode* FindParentWidget(const std::string& id);
    const UIWidgetNode* FindParentWidget(const std::string& id) const;
    std::vector<UIWidgetNode*> GetWidgetPath(const std::string& id);
    std::vector<const UIWidgetNode*> GetWidgetPath(const std::string& id) const;
    std::string GetWidgetPathString(const std::string& id, const char* separator = "/") const;

    std::string MakeUniqueWidgetId(const std::string& desiredId) const;
    static std::string SanitizeWidgetId(const std::string& value);

    void MarkDirty();

private:
    void EnsureIdCache() const;
    void NormalizeWidgetIds();
    std::filesystem::path m_Path;
    UIAsset m_Asset;
    std::string m_LastError;
    uint64 m_Revision = 1;
    bool m_Dirty = false;
    mutable uint64 m_IdCacheRevision = 0;
    mutable std::unordered_set<std::string> m_CachedWidgetIds;
};

const char* GetUIWidgetTypeName(UIWidgetType type);
UIWidgetType ParseUIWidgetType(const std::string& name);

} // namespace Dot
