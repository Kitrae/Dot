#include "UIWorkspace.h"

#include "Core/Assets/AssetManager.h"
#include "Panels/EditorPanel.h"
#include "UI/UIAssetDocument.h"
#include "../Utils/FileDialogs.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <functional>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <imgui.h>

namespace Dot
{

namespace
{

constexpr const char* kUiFileFilterDesc = "UI Files";
constexpr const char* kUiFileFilterExt = "*.dotui";
constexpr float kPresetEpsilon = 0.001f;

struct LayoutPreset
{
    const char* name = "";
    float anchorMinX = 0.0f;
    float anchorMinY = 0.0f;
    float anchorMaxX = 0.0f;
    float anchorMaxY = 0.0f;
    float alignX = 0.0f;
    float alignY = 0.0f;
};

constexpr LayoutPreset kAnchorPresets[] = {
    {"Top Left", 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f},
    {"Top Center", 0.5f, 0.0f, 0.5f, 0.0f, 0.5f, 0.0f},
    {"Top Right", 1.0f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f},
    {"Center Left", 0.0f, 0.5f, 0.0f, 0.5f, 0.0f, 0.5f},
    {"Center", 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f},
    {"Center Right", 1.0f, 0.5f, 1.0f, 0.5f, 1.0f, 0.5f},
    {"Bottom Left", 0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 1.0f},
    {"Bottom Center", 0.5f, 1.0f, 0.5f, 1.0f, 0.5f, 1.0f},
    {"Bottom Right", 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f},
    {"Fill Parent", 0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f},
};

constexpr LayoutPreset kAlignmentPresets[] = {
    {"Top Left", 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f},
    {"Top Center", 0.0f, 0.0f, 0.0f, 0.0f, 0.5f, 0.0f},
    {"Top Right", 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f},
    {"Center Left", 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.5f},
    {"Center", 0.0f, 0.0f, 0.0f, 0.0f, 0.5f, 0.5f},
    {"Center Right", 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.5f},
    {"Bottom Left", 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f},
    {"Bottom Center", 0.0f, 0.0f, 0.0f, 0.0f, 0.5f, 1.0f},
    {"Bottom Right", 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f},
};

ImVec4 ColorFromStyle(float r, float g, float b, float a)
{
    return ImVec4(r, g, b, a);
}

bool NearlyEqual(float a, float b)
{
    return std::fabs(a - b) <= kPresetEpsilon;
}

int FindAnchorPresetIndex(const UILayout& layout)
{
    for (int i = 0; i < static_cast<int>(std::size(kAnchorPresets)); ++i)
    {
        const LayoutPreset& preset = kAnchorPresets[i];
        if (NearlyEqual(layout.anchorMinX, preset.anchorMinX) &&
            NearlyEqual(layout.anchorMinY, preset.anchorMinY) &&
            NearlyEqual(layout.anchorMaxX, preset.anchorMaxX) &&
            NearlyEqual(layout.anchorMaxY, preset.anchorMaxY) &&
            NearlyEqual(layout.alignX, preset.alignX) &&
            NearlyEqual(layout.alignY, preset.alignY))
        {
            return i;
        }
    }

    return -1;
}

int FindAlignmentPresetIndex(const UILayout& layout)
{
    for (int i = 0; i < static_cast<int>(std::size(kAlignmentPresets)); ++i)
    {
        const LayoutPreset& preset = kAlignmentPresets[i];
        if (NearlyEqual(layout.alignX, preset.alignX) && NearlyEqual(layout.alignY, preset.alignY))
            return i;
    }

    return -1;
}

void ApplyAnchorPreset(UILayout& layout, const LayoutPreset& preset)
{
    layout.anchorMinX = preset.anchorMinX;
    layout.anchorMinY = preset.anchorMinY;
    layout.anchorMaxX = preset.anchorMaxX;
    layout.anchorMaxY = preset.anchorMaxY;
    layout.alignX = preset.alignX;
    layout.alignY = preset.alignY;

    if (preset.anchorMinX != preset.anchorMaxX || preset.anchorMinY != preset.anchorMaxY)
    {
        layout.offsetX = 0.0f;
        layout.offsetY = 0.0f;
    }
}

void ApplyAlignmentPreset(UILayout& layout, const LayoutPreset& preset)
{
    layout.alignX = preset.alignX;
    layout.alignY = preset.alignY;
}

std::string ToLowerCopy(const std::string& value)
{
    std::string lowered = value;
    std::transform(lowered.begin(), lowered.end(), lowered.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return lowered;
}

void AppendLowercaseSearchField(std::string& value, std::string_view field)
{
    if (field.empty())
        return;

    if (!value.empty())
        value.push_back('\n');

    const size_t baseSize = value.size();
    value.resize(baseSize + field.size());
    std::transform(field.begin(), field.end(), value.begin() + static_cast<std::ptrdiff_t>(baseSize),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
}

std::string ToAssetRelativePath(const std::filesystem::path& path)
{
    const std::string& rootString = AssetManager::Get().GetRootPath();
    if (rootString.empty())
        return path.generic_string();

    const std::filesystem::path rootPath(rootString);
    std::error_code ec;
    const std::filesystem::path relativePath = std::filesystem::relative(path, rootPath, ec);
    if (ec || relativePath.empty())
        return path.generic_string();
    return relativePath.generic_string();
}

void CollectAssetOptionsRecursive(const std::filesystem::path& rootPath, const std::unordered_set<std::string>& extensions,
                                  std::vector<std::string>& outOptions)
{
    std::error_code ec;
    if (!std::filesystem::exists(rootPath, ec))
        return;

    for (std::filesystem::recursive_directory_iterator it(rootPath, ec), end; it != end; it.increment(ec))
    {
        if (ec)
            break;
        if (!it->is_regular_file(ec))
            continue;

        std::string extension = ToLowerCopy(it->path().extension().string());
        if (!extensions.empty() && extensions.find(extension) == extensions.end())
            continue;
        outOptions.push_back(ToAssetRelativePath(it->path()));
    }
}

std::vector<std::string> CollectAssetOptions(std::initializer_list<const char*> extensions)
{
    std::vector<std::string> options;
    std::unordered_set<std::string> normalizedExtensions;
    for (const char* extension : extensions)
    {
        if (extension && extension[0] != '\0')
            normalizedExtensions.insert(ToLowerCopy(extension));
    }

    const std::string& rootString = AssetManager::Get().GetRootPath();
    if (!rootString.empty())
        CollectAssetOptionsRecursive(std::filesystem::path(rootString), normalizedExtensions, options);

    std::sort(options.begin(), options.end());
    options.erase(std::unique(options.begin(), options.end()), options.end());
    return options;
}

bool DrawStringSuggestionCombo(const char* label, std::string& value, const std::vector<std::string>& options)
{
    bool changed = false;
    const char* preview = value.empty() ? "(none)" : value.c_str();
    if (ImGui::BeginCombo(label, preview))
    {
        if (ImGui::Selectable("(none)", value.empty()))
        {
            value.clear();
            changed = true;
        }

        for (const std::string& option : options)
        {
            const bool selected = (option == value);
            if (ImGui::Selectable(option.c_str(), selected))
            {
                value = option;
                changed = true;
            }
            if (selected)
                ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    return changed;
}

bool DrawAssetPicker(const char* label, const char* browseButtonId, const FileFilter& filter,
                     const std::function<const std::vector<std::string>&()>& getOptions, std::string& value)
{
    bool changed = false;
    const char* preview = value.empty() ? "(none)" : value.c_str();
    if (ImGui::BeginCombo(label, preview))
    {
        const std::vector<std::string>& options = getOptions();
        if (ImGui::Selectable("(none)", value.empty()))
        {
            value.clear();
            changed = true;
        }

        for (const std::string& option : options)
        {
            const bool selected = (option == value);
            if (ImGui::Selectable(option.c_str(), selected))
            {
                value = option;
                changed = true;
            }
            if (selected)
                ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }

    ImGui::SameLine();
    if (ImGui::SmallButton(browseButtonId))
    {
        const std::string selectedPath = FileDialogs::OpenFile(filter);
        if (!selectedPath.empty())
        {
            value = ToAssetRelativePath(std::filesystem::path(selectedPath));
            changed = true;
        }
    }
    if (!value.empty())
    {
        ImGui::SameLine();
        const std::string clearId = std::string("Clear##") + label;
        if (ImGui::SmallButton(clearId.c_str()))
        {
            value.clear();
            changed = true;
        }
    }
    return changed;
}

bool DrawHalfWidthButton(const char* label, float width)
{
    return ImGui::Button(label, ImVec2(width, 0.0f));
}

bool DrawLabeledCombo(const char* label, const char* comboId, const char* preview,
                      const std::function<void()>& drawContents)
{
    ImGui::TextDisabled("%s", label);
    ImGui::SetNextItemWidth(-1.0f);
    if (!ImGui::BeginCombo(comboId, preview))
        return false;
    drawContents();
    ImGui::EndCombo();
    return true;
}

bool DrawLabeledDragFloat2(const char* label, const char* xId, const char* yId,
                           float& x, float& y, float speed, float minValue, float maxValue)
{
    ImGui::TextDisabled("%s", label);
    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    const float width = (ImGui::GetContentRegionAvail().x - spacing) * 0.5f;
    bool changed = false;
    ImGui::SetNextItemWidth(width);
    changed |= ImGui::DragFloat(xId, &x, speed, minValue, maxValue, "%.3f");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(width);
    changed |= ImGui::DragFloat(yId, &y, speed, minValue, maxValue, "%.3f");
    return changed;
}

bool DrawLabeledDragInt(const char* label, const char* id, int& value, float speed)
{
    ImGui::TextDisabled("%s", label);
    ImGui::SetNextItemWidth(-1.0f);
    return ImGui::DragInt(id, &value, speed);
}

bool DrawLabeledCheckbox(const char* label, const char* id, bool& value)
{
    ImGui::TextDisabled("%s", label);
    return ImGui::Checkbox(id, &value);
}

UIWidgetNode MakeWidgetTemplate(const char* label)
{
    UIWidgetNode node;
    node.name = label ? label : "Widget";
    node.id = node.name;
    node.layout.anchorMinX = 0.0f;
    node.layout.anchorMinY = 0.0f;
    node.layout.anchorMaxX = 0.0f;
    node.layout.anchorMaxY = 0.0f;
    node.layout.offsetX = 32.0f;
    node.layout.offsetY = 32.0f;
    node.layout.sizeX = 220.0f;
    node.layout.sizeY = 56.0f;
    node.layout.alignX = 0.0f;
    node.layout.alignY = 0.0f;
    node.layout.visible = true;
    node.style.opacity = 1.0f;

    if (std::strcmp(label, "Text") == 0)
    {
        node.type = UIWidgetType::Text;
        node.text = "Text";
        node.layout.sizeX = 180.0f;
        node.layout.sizeY = 28.0f;
    }
    else if (std::strcmp(label, "Image") == 0)
    {
        node.type = UIWidgetType::Image;
        node.layout.sizeX = 200.0f;
        node.layout.sizeY = 120.0f;
        node.style.imagePath = "Textures/";
    }
    else if (std::strcmp(label, "Button") == 0)
    {
        node.type = UIWidgetType::Button;
        node.text = "Button";
        node.layout.sizeX = 220.0f;
        node.layout.sizeY = 42.0f;
    }
    else if (std::strcmp(label, "Progress") == 0)
    {
        node.type = UIWidgetType::ProgressBar;
        node.text = "Progress";
        node.layout.sizeX = 240.0f;
        node.layout.sizeY = 24.0f;
        node.progress = 0.5f;
    }
    else if (std::strcmp(label, "Spacer") == 0)
    {
        node.type = UIWidgetType::Spacer;
        node.layout.sizeX = 120.0f;
        node.layout.sizeY = 24.0f;
        node.layout.visible = false;
    }
    else
    {
        node.type = UIWidgetType::Panel;
        node.layout.sizeX = 240.0f;
        node.layout.sizeY = 120.0f;
    }

    return node;
}

} // namespace

UIWorkspace::UIWorkspace() : Workspace("UI", WorkspaceType::UI), m_Document(std::make_unique<UIAssetDocument>())
{
    NewAsset();
}

void UIWorkspace::OnActivate()
{
    if (m_SelectedNodeId.empty())
        m_SelectedNodeId = m_Document->GetRoot().id;
}

void UIWorkspace::OnDeactivate()
{
}

bool UIWorkspace::OpenAsset(const std::filesystem::path& path)
{
    if (!m_Document->Load(path))
    {
        m_StatusIsError = true;
        m_StatusMessage = m_Document->GetLastError();
        return false;
    }

    m_SelectedNodeId = m_Document->GetRoot().id;
    m_HierarchySearchBuffer[0] = '\0';
    m_StatusIsError = false;
    m_StatusMessage = "Opened " + path.filename().string();
    return true;
}

bool UIWorkspace::NewAsset()
{
    m_Document->New();
    m_SelectedNodeId = m_Document->GetRoot().id;
    m_HierarchySearchBuffer[0] = '\0';
    m_StatusIsError = false;
    m_StatusMessage = "New UI document";
    return true;
}

bool UIWorkspace::SaveAsset()
{
    const bool saved = m_Document->HasPath() ? m_Document->Save() : SaveAssetAs();
    m_StatusIsError = !saved;
    if (saved)
        m_StatusMessage = "Saved " + m_Document->GetPath().filename().string();
    else if (m_Document->GetLastError().empty())
        m_StatusMessage = "Failed to save UI document";
    else
        m_StatusMessage = m_Document->GetLastError();
    return saved;
}

bool UIWorkspace::SaveAssetAs()
{
    std::filesystem::path currentPath = m_Document->GetPath();
    std::string defaultName = currentPath.empty() ? "Untitled.dotui" : currentPath.filename().string();
    FileFilter filter{kUiFileFilterDesc, kUiFileFilterExt};
    const std::string savePath = FileDialogs::SaveFile(filter, defaultName);
    if (savePath.empty())
        return false;

    const bool saved = m_Document->SaveAs(savePath);
    m_StatusIsError = !saved;
    if (saved)
        m_StatusMessage = "Saved " + std::filesystem::path(savePath).filename().string();
    else
        m_StatusMessage = m_Document->GetLastError();
    return saved;
}

const std::filesystem::path& UIWorkspace::GetAssetPath() const
{
    return m_Document->GetPath();
}

bool UIWorkspace::HasOpenAsset() const
{
    return m_Document->HasPath();
}

bool UIWorkspace::IsAssetDirty() const
{
    return m_Document->IsDirty();
}

void UIWorkspace::HandleKeyboardShortcuts()
{
    if (!ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) || ImGui::IsAnyItemActive())
        return;

    const ImGuiIO& io = ImGui::GetIO();
    if (io.KeyCtrl && io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_S, false))
    {
        SaveAssetAs();
        return;
    }

    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S, false))
    {
        SaveAsset();
        return;
    }

    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_N, false))
    {
        NewAsset();
        return;
    }

    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_O, false))
    {
        FileFilter filter{kUiFileFilterDesc, kUiFileFilterExt};
        const std::string openPath = FileDialogs::OpenFile(filter);
        if (!openPath.empty())
            OpenAsset(openPath);
        return;
    }

    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_F, false))
    {
        m_FocusHierarchySearch = true;
        return;
    }

    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_D, false))
    {
        DuplicateSelectedNode();
        return;
    }

    if (ImGui::IsKeyPressed(ImGuiKey_Delete, false) || ImGui::IsKeyPressed(ImGuiKey_Backspace, false))
    {
        DeleteSelectedNode();
        return;
    }

    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false))
    {
        if (m_HierarchySearchBuffer[0] != '\0')
            m_HierarchySearchBuffer[0] = '\0';
    }
}

void UIWorkspace::OnImGui()
{
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);

    ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
                                   ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                                   ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus |
                                   ImGuiWindowFlags_MenuBar;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6.0f, 6.0f));

    if (ImGui::Begin("##UIWorkspace", nullptr, windowFlags))
    {
        HandleKeyboardShortcuts();
        DrawMenuBar();

        const ImVec2 contentSize = ImGui::GetContentRegionAvail();
        const float splitterWidth = 4.0f;
        const float availableWidth = std::max(0.0f, contentSize.x - m_HierarchyWidth - m_InspectorWidth - (splitterWidth * 2.0f));
        const float centerWidth = std::max(220.0f, availableWidth);
        const float panelHeight = contentSize.y;

        ImGui::BeginChild("##UIHierarchy", ImVec2(m_HierarchyWidth, panelHeight), true);
        DrawHierarchyPanel();
        ImGui::EndChild();

        ImGui::SameLine();
        ImGui::Button("##UIHierarchySplitter", ImVec2(splitterWidth, panelHeight));
        if (ImGui::IsItemActive())
        {
            m_HierarchyWidth += ImGui::GetIO().MouseDelta.x;
            m_HierarchyWidth = std::clamp(m_HierarchyWidth, 180.0f, 420.0f);
        }
        if (ImGui::IsItemHovered())
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);

        ImGui::SameLine();

        ImGui::BeginChild("##UICanvas", ImVec2(centerWidth, panelHeight), true);
        DrawCanvasPanel();
        ImGui::EndChild();

        ImGui::SameLine();
        ImGui::Button("##UIInspectorSplitter", ImVec2(splitterWidth, panelHeight));
        if (ImGui::IsItemActive())
        {
            m_InspectorWidth -= ImGui::GetIO().MouseDelta.x;
            m_InspectorWidth = std::clamp(m_InspectorWidth, 220.0f, 520.0f);
        }
        if (ImGui::IsItemHovered())
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);

        ImGui::SameLine();

        ImGui::BeginChild("##UIInspector", ImVec2(m_InspectorWidth, panelHeight), true);
        DrawInspectorPanel();
        ImGui::EndChild();
    }
    ImGui::End();

    ImGui::PopStyleVar(3);
}

void UIWorkspace::DrawMenuBar()
{
    if (!ImGui::BeginMenuBar())
        return;

    if (ImGui::BeginMenu("File"))
    {
        if (ImGui::MenuItem("New", "Ctrl+N"))
            NewAsset();
        if (ImGui::MenuItem("Open...", "Ctrl+O"))
        {
            FileFilter filter{kUiFileFilterDesc, kUiFileFilterExt};
            const std::string openPath = FileDialogs::OpenFile(filter);
            if (!openPath.empty())
                OpenAsset(openPath);
        }
        if (ImGui::MenuItem("Save", "Ctrl+S"))
            SaveAsset();
        if (ImGui::MenuItem("Save As...", "Ctrl+Shift+S"))
            SaveAssetAs();
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Create"))
    {
        if (ImGui::MenuItem("Child/Panel"))
            AddNodeToSelection(MakeNodeTemplate("Panel"));
        if (ImGui::MenuItem("Sibling/Panel"))
            AddNodeAsSiblingToSelection(MakeNodeTemplate("Panel"));
        ImGui::Separator();
        if (ImGui::MenuItem("Panel"))
            AddNodeToSelection(MakeNodeTemplate("Panel"));
        if (ImGui::MenuItem("Text"))
            AddNodeToSelection(MakeNodeTemplate("Text"));
        if (ImGui::MenuItem("Button"))
            AddNodeToSelection(MakeNodeTemplate("Button"));
        if (ImGui::MenuItem("Image"))
            AddNodeToSelection(MakeNodeTemplate("Image"));
        if (ImGui::MenuItem("Progress Bar"))
            AddNodeToSelection(MakeNodeTemplate("Progress"));
        if (ImGui::MenuItem("Spacer"))
            AddNodeToSelection(MakeNodeTemplate("Spacer"));
        ImGui::EndMenu();
    }

    ImGui::SameLine();
    ImGui::TextDisabled("UI");
    ImGui::SameLine();
    const std::string currentName = m_Document->GetPath().empty() ? std::string("Untitled")
                                                                 : m_Document->GetPath().filename().string();
    const std::string currentLabel = currentName + (m_Document->IsDirty() ? " *" : "");
    ImGui::TextColored(m_Document->IsDirty() ? ImVec4(0.95f, 0.72f, 0.3f, 1.0f) : ImVec4(0.55f, 0.75f, 0.95f, 1.0f),
                       "%s", currentLabel.c_str());

    ImGui::SameLine(ImGui::GetWindowWidth() - ImGui::CalcTextSize(m_StatusMessage.c_str()).x - 12.0f);
    if (!m_StatusMessage.empty())
        ImGui::TextColored(m_StatusIsError ? ImVec4(0.92f, 0.42f, 0.42f, 1.0f) : ImVec4(0.56f, 0.72f, 0.86f, 1.0f),
                           "%s", m_StatusMessage.c_str());

    ImGui::EndMenuBar();
}

void UIWorkspace::DrawHierarchyPanel()
{
    DrawHierarchySearchBar();
    DrawCreateQuickActions();
    ImGui::Separator();

    const std::string hierarchyFilter =
        m_HierarchySearchBuffer[0] == '\0' ? std::string() : ToLowerCopy(m_HierarchySearchBuffer);
    RebuildHierarchyCache(hierarchyFilter);
    if (!hierarchyFilter.empty())
        ImGui::TextDisabled("%d matches", m_HierarchyVisibleCount);
    else
        ImGui::TextDisabled("%d widgets", m_HierarchyVisibleCount);

    DrawWidgetTree(m_Document->GetRoot(), hierarchyFilter.empty() ? nullptr : &hierarchyFilter);
}

void UIWorkspace::DrawWidgetTree(UIWidgetNode& node, const std::string* filterLower)
{
    if (!IsNodeVisibleInHierarchy(node, filterLower))
        return;

    const bool isLeaf = node.children.empty();
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_OpenOnArrow;
    if (isLeaf)
        flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
    if (m_SelectedNodeId == node.id)
        flags |= ImGuiTreeNodeFlags_Selected;

    if (filterLower && !filterLower->empty() && !isLeaf)
        ImGui::SetNextItemOpen(true, ImGuiCond_Always);

    const std::string label = std::string(GetUIWidgetTypeName(node.type)) + "  " + (node.name.empty() ? node.id : node.name);
    const bool open = ImGui::TreeNodeEx(node.id.c_str(), flags, "%s", label.c_str());
    if (ImGui::IsItemClicked())
        SelectNode(node.id);

    if (node.id != m_Document->GetRoot().id)
    {
        if (ImGui::BeginDragDropSource())
        {
            ImGui::SetDragDropPayload("UI_WIDGET_NODE", node.id.c_str(), node.id.size() + 1);
            ImGui::TextUnformatted(label.c_str());
            ImGui::EndDragDropSource();
        }

        if (ImGui::BeginDragDropTarget())
        {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("UI_WIDGET_NODE"))
            {
                const char* payloadId = static_cast<const char*>(payload->Data);
                if (payloadId && payloadId[0] != '\0')
                {
                    if (MoveNodeToParent(payloadId, node.id))
                        SelectNode(payloadId);
                }
            }
            ImGui::EndDragDropTarget();
        }

        const ImVec2 itemMin = ImGui::GetItemRectMin();
        const ImVec2 itemMax = ImGui::GetItemRectMax();
        const float dropZoneHeight = 6.0f;
        ImGui::SetCursorScreenPos(ImVec2(itemMin.x, itemMax.y - (dropZoneHeight * 0.5f)));
        ImGui::InvisibleButton((std::string("##UIHierarchyDropAfter_") + node.id).c_str(),
                               ImVec2(std::max(1.0f, itemMax.x - itemMin.x), dropZoneHeight));
        if (ImGui::BeginDragDropTarget())
        {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("UI_WIDGET_NODE"))
            {
                const char* payloadId = static_cast<const char*>(payload->Data);
                if (payloadId && payloadId[0] != '\0')
                {
                    if (MoveNodeAfter(payloadId, node.id))
                        SelectNode(payloadId);
                }
            }
            ImGui::EndDragDropTarget();
        }
        if (ImGui::IsItemHovered())
        {
            ImGui::GetWindowDrawList()->AddLine(ImVec2(itemMin.x, itemMax.y + 1.0f), ImVec2(itemMax.x, itemMax.y + 1.0f),
                                                IM_COL32(118, 210, 255, 200), 2.0f);
        }
    }

    if (open)
    {
        for (UIWidgetNode& child : node.children)
            DrawWidgetTree(child, filterLower);
        if (!isLeaf)
            ImGui::TreePop();
    }
}

void UIWorkspace::DrawHierarchySearchBar()
{
    ImGui::TextUnformatted("Hierarchy");
    ImGui::Separator();

    ImGui::SetNextItemWidth(-1.0f);
    if (m_FocusHierarchySearch)
    {
        ImGui::SetKeyboardFocusHere();
        m_FocusHierarchySearch = false;
    }
    ImGui::InputTextWithHint("##UIHierarchySearch", "Search widgets, ids, text, bindings...", m_HierarchySearchBuffer,
                             sizeof(m_HierarchySearchBuffer));

    if (ImGui::IsItemActive() && ImGui::IsKeyPressed(ImGuiKey_Escape))
        m_HierarchySearchBuffer[0] = '\0';

    if (m_HierarchySearchBuffer[0] != '\0')
    {
        ImGui::SameLine();
        if (ImGui::SmallButton("Clear"))
            m_HierarchySearchBuffer[0] = '\0';
    }
}

void UIWorkspace::RebuildHierarchyCache(const std::string& filterLower)
{
    const uint64_t revision = m_Document->GetRevision();
    if (m_HierarchyCacheRevision == revision && m_HierarchyCacheFilter == filterLower)
        return;

    m_HierarchyVisibleNodeIds.clear();
    m_HierarchyVisibleCount = 0;

    int visibleCount = 0;
    CollectHierarchyMatches(m_Document->GetRoot(), filterLower, !filterLower.empty(), visibleCount);
    m_HierarchyVisibleCount = visibleCount;
    m_HierarchyCacheFilter = filterLower;
    m_HierarchyCacheRevision = revision;
}

bool UIWorkspace::CollectHierarchyMatches(const UIWidgetNode& node, const std::string& filterLower, bool filterActive, int& visibleCount)
{
    const bool selfMatches = !filterActive || NodeMatchesHierarchyFilter(node, filterLower);
    bool subtreeMatches = selfMatches;

    for (const UIWidgetNode& child : node.children)
    {
        if (CollectHierarchyMatches(child, filterLower, filterActive, visibleCount))
            subtreeMatches = true;
    }

    if (subtreeMatches)
        m_HierarchyVisibleNodeIds.insert(node.id);
    if (selfMatches)
        ++visibleCount;
    return subtreeMatches;
}

void UIWorkspace::EnsureNodeCache() const
{
    const uint64_t revision = m_Document->GetRevision();
    if (m_NodeCacheRevision == revision)
        return;

    m_NodeCache.clear();

    const UIWidgetNode& rootNode = m_Document->GetRoot();
    const std::string rootPath = rootNode.name.empty() ? rootNode.id : rootNode.name;
    BuildNodeCacheRecursive(rootNode, nullptr, rootPath);
    m_NodeCacheRevision = revision;
}

void UIWorkspace::BuildNodeCacheRecursive(const UIWidgetNode& node, const UIWidgetNode* parent, const std::string& path) const
{
    NodeCacheEntry entry;
    entry.node = &node;
    entry.parent = parent;
    entry.path = path;
    AppendLowercaseSearchField(entry.searchText, GetUIWidgetTypeName(node.type));
    AppendLowercaseSearchField(entry.searchText, node.name);
    AppendLowercaseSearchField(entry.searchText, node.id);
    AppendLowercaseSearchField(entry.searchText, node.text);
    AppendLowercaseSearchField(entry.searchText, node.bindingKey);
    AppendLowercaseSearchField(entry.searchText, node.style.fontPath);
    AppendLowercaseSearchField(entry.searchText, node.style.imagePath);
    AppendLowercaseSearchField(entry.searchText, node.style.materialPath);
    m_NodeCache.emplace(node.id, std::move(entry));

    for (const UIWidgetNode& child : node.children)
    {
        const std::string childLabel = child.name.empty() ? child.id : child.name;
        const std::string childPath = path.empty() ? childLabel : (path + " / " + childLabel);
        BuildNodeCacheRecursive(child, &node, childPath);
    }
}

void UIWorkspace::EnsureDocumentSuggestionCache()
{
    const uint64_t revision = m_Document->GetRevision();
    if (m_DocumentSuggestionCacheRevision == revision)
        return;

    EnsureNodeCache();

    std::unordered_set<std::string> bindingValues;
    std::unordered_set<std::string> eventValues;
    for (const auto& [id, entry] : m_NodeCache)
    {
        (void)id;
        if (!entry.node)
            continue;
        if (!entry.node->bindingKey.empty())
            bindingValues.insert(entry.node->bindingKey);
        if (!entry.node->onClickEvent.empty())
            eventValues.insert(entry.node->onClickEvent);
        if (!entry.node->onChangeEvent.empty())
            eventValues.insert(entry.node->onChangeEvent);
    }

    m_BindingSuggestionOptions.assign(bindingValues.begin(), bindingValues.end());
    std::sort(m_BindingSuggestionOptions.begin(), m_BindingSuggestionOptions.end());
    m_EventSuggestionOptions.assign(eventValues.begin(), eventValues.end());
    std::sort(m_EventSuggestionOptions.begin(), m_EventSuggestionOptions.end());
    m_DocumentSuggestionCacheRevision = revision;
}

void UIWorkspace::InvalidateAssetOptionCachesIfNeeded()
{
    const std::string& rootPath = AssetManager::Get().GetRootPath();
    if (m_AssetOptionCacheRoot == rootPath)
        return;

    m_AssetOptionCacheRoot = rootPath;
    m_FontAssetOptions.clear();
    m_ImageAssetOptions.clear();
    m_MaterialAssetOptions.clear();
}

const std::vector<std::string>& UIWorkspace::GetFontAssetOptions()
{
    InvalidateAssetOptionCachesIfNeeded();
    if (m_FontAssetOptions.empty())
        m_FontAssetOptions = CollectAssetOptions({".ttf", ".otf"});
    return m_FontAssetOptions;
}

const std::vector<std::string>& UIWorkspace::GetImageAssetOptions()
{
    InvalidateAssetOptionCachesIfNeeded();
    if (m_ImageAssetOptions.empty())
        m_ImageAssetOptions = CollectAssetOptions({".png", ".jpg", ".jpeg", ".tga", ".bmp"});
    return m_ImageAssetOptions;
}

const std::vector<std::string>& UIWorkspace::GetMaterialAssetOptions()
{
    InvalidateAssetOptionCachesIfNeeded();
    if (m_MaterialAssetOptions.empty())
        m_MaterialAssetOptions = CollectAssetOptions({".dotmat"});
    return m_MaterialAssetOptions;
}

void UIWorkspace::DrawCreateQuickActions()
{
    ImGui::TextDisabled("Create");
    if (ImGui::SmallButton("Child"))
        AddNodeToSelection(MakeNodeTemplate("Panel"));
    ImGui::SameLine();
    if (ImGui::SmallButton("Sibling"))
        AddNodeAsSiblingToSelection(MakeNodeTemplate("Panel"));
    ImGui::SameLine();
    if (ImGui::SmallButton("Duplicate"))
        DuplicateSelectedNode();
    ImGui::SameLine();
    if (ImGui::SmallButton("Delete"))
        DeleteSelectedNode();

    if (ImGui::Button("Panel"))
        AddNodeToSelection(MakeNodeTemplate("Panel"));
    ImGui::SameLine();
    if (ImGui::Button("Text"))
        AddNodeToSelection(MakeNodeTemplate("Text"));
    ImGui::SameLine();
    if (ImGui::Button("Button"))
        AddNodeToSelection(MakeNodeTemplate("Button"));
    ImGui::SameLine();
    if (ImGui::Button("Image"))
        AddNodeToSelection(MakeNodeTemplate("Image"));
    ImGui::SameLine();
    if (ImGui::Button("Progress"))
        AddNodeToSelection(MakeNodeTemplate("Progress"));
    ImGui::SameLine();
    if (ImGui::Button("Spacer"))
        AddNodeToSelection(MakeNodeTemplate("Spacer"));
}

void UIWorkspace::DrawCanvasPanel()
{
    ImGui::TextUnformatted("Canvas");
    ImGui::Separator();
    ImGui::TextDisabled("Click nodes in the canvas or hierarchy to inspect them.");
    ImGui::Spacing();

    const ImVec2 canvasStart = ImGui::GetCursorScreenPos();
    const ImVec2 canvasAvail = ImGui::GetContentRegionAvail();
    const ImVec2 canvasEnd = ImVec2(canvasStart.x + canvasAvail.x, canvasStart.y + std::max(220.0f, canvasAvail.y));
    ImGui::InvisibleButton("##UIWorkspaceCanvas", ImVec2(canvasAvail.x, std::max(220.0f, canvasAvail.y)));

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    drawList->AddRectFilled(canvasStart, canvasEnd, IM_COL32(20, 24, 29, 255), 6.0f);
    drawList->AddRect(canvasStart, canvasEnd, IM_COL32(52, 61, 74, 255), 6.0f, 0, 1.0f);

    const float gridStep = 24.0f;
    for (float x = canvasStart.x + fmodf(canvasStart.x, gridStep); x < canvasEnd.x; x += gridStep)
        drawList->AddLine(ImVec2(x, canvasStart.y), ImVec2(x, canvasEnd.y), IM_COL32(36, 42, 50, 100));
    for (float y = canvasStart.y + fmodf(canvasStart.y, gridStep); y < canvasEnd.y; y += gridStep)
        drawList->AddLine(ImVec2(canvasStart.x, y), ImVec2(canvasEnd.x, y), IM_COL32(36, 42, 50, 100));

    std::vector<CanvasDrawItem> liveItems;
    const std::vector<CanvasDrawItem>* itemsPtr = nullptr;
    if (m_CanvasDragging || m_CanvasResizing)
    {
        CollectCanvasDrawItems(canvasStart, canvasEnd, liveItems);
        itemsPtr = &liveItems;
    }
    else
    {
        EnsureCanvasDrawCache(canvasStart, canvasEnd);
        itemsPtr = &m_CanvasDrawCache;
    }
    const std::vector<CanvasDrawItem>& items = *itemsPtr;

    const CanvasDrawItem* selectedItem = nullptr;
    for (const CanvasDrawItem& item : items)
    {
        if (item.node && item.node->id == m_SelectedNodeId)
        {
            selectedItem = &item;
            break;
        }
    }

    const float resizeHandleHalfSize = 5.0f;
    auto getResizeHandleRect = [&](CanvasResizeHandle handle)
    {
        if (!selectedItem)
            return std::make_pair(ImVec2(), ImVec2());

        ImVec2 center = selectedItem->max;
        switch (handle)
        {
        case CanvasResizeHandle::TopLeft:
            center = selectedItem->min;
            break;
        case CanvasResizeHandle::TopRight:
            center = ImVec2(selectedItem->max.x, selectedItem->min.y);
            break;
        case CanvasResizeHandle::BottomLeft:
            center = ImVec2(selectedItem->min.x, selectedItem->max.y);
            break;
        case CanvasResizeHandle::BottomRight:
            center = selectedItem->max;
            break;
        default:
            break;
        }

        return std::make_pair(ImVec2(center.x - resizeHandleHalfSize, center.y - resizeHandleHalfSize),
                              ImVec2(center.x + resizeHandleHalfSize, center.y + resizeHandleHalfSize));
    };

    CanvasResizeHandle hoveredResizeHandle = CanvasResizeHandle::None;
    const ImVec2 mousePos = ImGui::GetIO().MousePos;
    if (selectedItem && selectedItem->node && selectedItem->node->id != m_Document->GetRoot().id)
    {
        const CanvasResizeHandle handles[] = {
            CanvasResizeHandle::TopLeft,
            CanvasResizeHandle::TopRight,
            CanvasResizeHandle::BottomLeft,
            CanvasResizeHandle::BottomRight};
        for (CanvasResizeHandle handle : handles)
        {
            const auto handleRect = getResizeHandleRect(handle);
            if (mousePos.x >= handleRect.first.x && mousePos.x <= handleRect.second.x &&
                mousePos.y >= handleRect.first.y && mousePos.y <= handleRect.second.y)
            {
                hoveredResizeHandle = handle;
                break;
            }
        }
    }

    if (hoveredResizeHandle != CanvasResizeHandle::None || m_CanvasResizing)
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNWSE);

    if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
    {
        bool hitAny = false;
        if (hoveredResizeHandle != CanvasResizeHandle::None && selectedItem && selectedItem->node)
        {
            hitAny = true;
            m_CanvasResizing = true;
            m_CanvasDragging = false;
            m_CanvasDragNodeId = selectedItem->node->id;
            m_CanvasDragMouseStart = mousePos;
            m_CanvasResizeStartOffsetX = selectedItem->node->layout.offsetX;
            m_CanvasResizeStartOffsetY = selectedItem->node->layout.offsetY;
            m_CanvasResizeStartSizeX = selectedItem->node->layout.sizeX;
            m_CanvasResizeStartSizeY = selectedItem->node->layout.sizeY;
            m_CanvasResizeHandle = hoveredResizeHandle;
        }
        else
        {
            for (auto it = items.rbegin(); it != items.rend(); ++it)
            {
                if (mousePos.x >= it->min.x && mousePos.x <= it->max.x && mousePos.y >= it->min.y && mousePos.y <= it->max.y)
                {
                    SelectNode(it->node->id);
                    hitAny = true;
                    if (it->node->id != m_Document->GetRoot().id)
                    {
                        m_CanvasDragging = true;
                        m_CanvasResizing = false;
                        m_CanvasResizeHandle = CanvasResizeHandle::None;
                        m_CanvasDragNodeId = it->node->id;
                        m_CanvasDragMouseStart = mousePos;
                        m_CanvasDragOffsetX = it->node->layout.offsetX;
                        m_CanvasDragOffsetY = it->node->layout.offsetY;
                    }
                    break;
                }
            }
        }

        if (!hitAny)
        {
            SelectNode(m_Document->GetRoot().id);
            m_CanvasDragging = false;
            m_CanvasResizing = false;
            m_CanvasResizeHandle = CanvasResizeHandle::None;
            m_CanvasDragNodeId.clear();
        }
    }

    if (m_CanvasDragging)
    {
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left))
        {
            if (UIWidgetNode* draggedNode = FindNodeById(m_CanvasDragNodeId))
            {
                const ImVec2 currentMousePos = ImGui::GetIO().MousePos;
                const ImVec2 mouseDelta(currentMousePos.x - m_CanvasDragMouseStart.x, currentMousePos.y - m_CanvasDragMouseStart.y);
                const float newOffsetX = m_CanvasDragOffsetX + mouseDelta.x;
                const float newOffsetY = m_CanvasDragOffsetY + mouseDelta.y;
                if (draggedNode->layout.offsetX != newOffsetX || draggedNode->layout.offsetY != newOffsetY)
                {
                    draggedNode->layout.offsetX = newOffsetX;
                    draggedNode->layout.offsetY = newOffsetY;
                    m_Document->SetDirty(true);
                    m_CanvasInteractionDirtyPending = true;
                }
            }
            else
            {
                if (m_CanvasInteractionDirtyPending)
                {
                    m_Document->MarkDirty();
                    m_CanvasInteractionDirtyPending = false;
                }
                m_CanvasDragging = false;
                m_CanvasDragNodeId.clear();
            }
        }
        else
        {
            if (m_CanvasInteractionDirtyPending)
            {
                m_Document->MarkDirty();
                m_CanvasInteractionDirtyPending = false;
            }
            m_CanvasDragging = false;
            m_CanvasDragNodeId.clear();
        }
    }

    if (m_CanvasResizing)
    {
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left))
        {
            if (UIWidgetNode* resizedNode = FindNodeById(m_CanvasDragNodeId))
            {
                const ImVec2 currentMousePos = ImGui::GetIO().MousePos;
                const ImVec2 mouseDelta(currentMousePos.x - m_CanvasDragMouseStart.x, currentMousePos.y - m_CanvasDragMouseStart.y);
                float newOffsetX = m_CanvasResizeStartOffsetX;
                float newOffsetY = m_CanvasResizeStartOffsetY;
                float newSizeX = m_CanvasResizeStartSizeX;
                float newSizeY = m_CanvasResizeStartSizeY;

                switch (m_CanvasResizeHandle)
                {
                case CanvasResizeHandle::TopLeft:
                    newOffsetX = m_CanvasResizeStartOffsetX + mouseDelta.x;
                    newOffsetY = m_CanvasResizeStartOffsetY + mouseDelta.y;
                    newSizeX = m_CanvasResizeStartSizeX - mouseDelta.x;
                    newSizeY = m_CanvasResizeStartSizeY - mouseDelta.y;
                    break;
                case CanvasResizeHandle::TopRight:
                    newOffsetY = m_CanvasResizeStartOffsetY + mouseDelta.y;
                    newSizeX = m_CanvasResizeStartSizeX + mouseDelta.x;
                    newSizeY = m_CanvasResizeStartSizeY - mouseDelta.y;
                    break;
                case CanvasResizeHandle::BottomLeft:
                    newOffsetX = m_CanvasResizeStartOffsetX + mouseDelta.x;
                    newSizeX = m_CanvasResizeStartSizeX - mouseDelta.x;
                    newSizeY = m_CanvasResizeStartSizeY + mouseDelta.y;
                    break;
                case CanvasResizeHandle::BottomRight:
                    newSizeX = m_CanvasResizeStartSizeX + mouseDelta.x;
                    newSizeY = m_CanvasResizeStartSizeY + mouseDelta.y;
                    break;
                default:
                    break;
                }

                resizedNode->layout.offsetX = newOffsetX;
                resizedNode->layout.offsetY = newOffsetY;
                resizedNode->layout.sizeX = std::max(8.0f, newSizeX);
                resizedNode->layout.sizeY = std::max(8.0f, newSizeY);
                m_Document->SetDirty(true);
                m_CanvasInteractionDirtyPending = true;
            }
            else
            {
                if (m_CanvasInteractionDirtyPending)
                {
                    m_Document->MarkDirty();
                    m_CanvasInteractionDirtyPending = false;
                }
                m_CanvasResizing = false;
                m_CanvasResizeHandle = CanvasResizeHandle::None;
                m_CanvasDragNodeId.clear();
            }
        }
        else
        {
            if (m_CanvasInteractionDirtyPending)
            {
                m_Document->MarkDirty();
                m_CanvasInteractionDirtyPending = false;
            }
            m_CanvasResizing = false;
            m_CanvasResizeHandle = CanvasResizeHandle::None;
        }
    }

    for (const CanvasDrawItem& item : items)
    {
        const bool selected = (m_SelectedNodeId == item.node->id);
        ImVec4 fillColor = item.node->layout.visible ? ColorFromStyle(item.node->style.backgroundR, item.node->style.backgroundG,
                                                                     item.node->style.backgroundB, item.node->style.backgroundA)
                                                    : ImVec4(0.12f, 0.12f, 0.13f, 0.55f);
        fillColor.w *= item.node->style.opacity;
        const ImVec4 borderColor = selected ? ImVec4(0.58f, 0.82f, 1.0f, 1.0f)
                                            : ColorFromStyle(item.node->style.borderR, item.node->style.borderG,
                                                             item.node->style.borderB, item.node->style.borderA);
        drawList->AddRectFilled(item.min, item.max, ImGui::ColorConvertFloat4ToU32(fillColor), 5.0f);
        drawList->AddRect(item.min, item.max, ImGui::ColorConvertFloat4ToU32(borderColor),
                          5.0f, 0, selected ? 2.0f : std::max(1.0f, item.node->style.borderThickness));

        if (selected && item.node->id != m_Document->GetRoot().id)
        {
            const CanvasResizeHandle handles[] = {
                CanvasResizeHandle::TopLeft,
                CanvasResizeHandle::TopRight,
                CanvasResizeHandle::BottomLeft,
                CanvasResizeHandle::BottomRight};
            for (CanvasResizeHandle handle : handles)
            {
                const auto handleRect = getResizeHandleRect(handle);
                const bool activeHandle = m_CanvasResizing && m_CanvasResizeHandle == handle;
                const bool hoveredHandle = hoveredResizeHandle == handle;
                const ImU32 handleColor = activeHandle ? IM_COL32(118, 210, 255, 255)
                                                       : (hoveredHandle ? IM_COL32(220, 242, 255, 255)
                                                                        : IM_COL32(180, 210, 240, 255));
                drawList->AddRectFilled(handleRect.first, handleRect.second, handleColor, 2.0f);
                drawList->AddRect(handleRect.first, handleRect.second, IM_COL32(28, 34, 42, 255), 2.0f, 0, 1.0f);
            }
        }

        const ImVec2 padding(item.node->style.paddingX, item.node->style.paddingY);
        const ImVec2 textPos(item.min.x + padding.x, item.min.y + padding.y);

        if (item.node->type == UIWidgetType::ProgressBar)
        {
            const float progress = std::clamp(item.node->progress, 0.0f, 1.0f);
            const ImVec2 fillMax(item.min.x + (item.max.x - item.min.x) * progress, item.max.y);
            drawList->AddRectFilled(item.min, fillMax, IM_COL32(92, 160, 225, 220), 5.0f);
            drawList->AddText(textPos, IM_COL32(240, 244, 250, 255), item.node->text.empty() ? "Progress" : item.node->text.c_str());
        }
        else if (item.node->type == UIWidgetType::Image)
        {
            drawList->AddText(textPos, IM_COL32(236, 240, 246, 255), item.node->name.empty() ? "Image" : item.node->name.c_str());
            if (!item.node->style.imagePath.empty())
            {
                drawList->AddText(ImVec2(textPos.x, textPos.y + 18.0f), IM_COL32(160, 174, 186, 255), "%s",
                                  item.node->style.imagePath.c_str());
            }
        }
        else if (item.node->type == UIWidgetType::Spacer)
        {
            drawList->AddText(textPos, IM_COL32(160, 168, 176, 255), "Spacer");
        }
        else
        {
            const char* label = item.node->text.empty() ? item.node->name.c_str() : item.node->text.c_str();
            drawList->AddText(textPos, IM_COL32(236, 240, 246, 255), "%s", label);
        }
    }
}

void UIWorkspace::CollectCanvasDrawItems(const ImVec2& canvasStart, const ImVec2& canvasEnd,
                                         std::vector<CanvasDrawItem>& items) const
{
    EnsureNodeCache();

    items.clear();
    items.reserve(std::max<size_t>(1, m_NodeCache.size()));

    int order = 0;
    DrawCanvasNodeRecursive(m_Document->GetRoot(), canvasStart, canvasEnd, items, 0, order);
    std::sort(items.begin(), items.end(),
              [](const CanvasDrawItem& a, const CanvasDrawItem& b)
              {
                  if (a.node->layout.zOrder != b.node->layout.zOrder)
                      return a.node->layout.zOrder < b.node->layout.zOrder;
                  return a.order < b.order;
              });
}

void UIWorkspace::EnsureCanvasDrawCache(const ImVec2& canvasStart, const ImVec2& canvasEnd) const
{
    const uint64_t revision = m_Document->GetRevision();
    if (m_CanvasDrawCacheRevision == revision &&
        m_CanvasDrawCacheStart.x == canvasStart.x && m_CanvasDrawCacheStart.y == canvasStart.y &&
        m_CanvasDrawCacheEnd.x == canvasEnd.x && m_CanvasDrawCacheEnd.y == canvasEnd.y)
    {
        return;
    }

    CollectCanvasDrawItems(canvasStart, canvasEnd, m_CanvasDrawCache);
    m_CanvasDrawCacheStart = canvasStart;
    m_CanvasDrawCacheEnd = canvasEnd;
    m_CanvasDrawCacheRevision = revision;
}

void UIWorkspace::DrawCanvasNodeRecursive(const UIWidgetNode& node, const ImVec2& parentMin, const ImVec2& parentMax,
                                          std::vector<CanvasDrawItem>& items, int depth, int& order) const
{
    const ImVec2 nodeMin = node.id == m_Document->GetRoot().id ? parentMin : ComputeNodeMin(node, parentMin, parentMax);
    const ImVec2 nodeMax = node.id == m_Document->GetRoot().id ? parentMax : ComputeNodeMax(node, parentMin, parentMax);

    if (node.id != m_Document->GetRoot().id || depth == 0)
    {
        CanvasDrawItem item;
        item.node = &node;
        item.min = nodeMin;
        item.max = nodeMax;
        item.depth = depth;
        item.order = order++;
        items.push_back(item);
    }

    for (const UIWidgetNode& child : node.children)
        DrawCanvasNodeRecursive(child, nodeMin, nodeMax, items, depth + 1, order);
}

void UIWorkspace::DrawInspectorPanel()
{
    ImGui::TextUnformatted("Inspector");
    ImGui::Separator();

    DrawInspectorForSelectedNode();
}

UIWidgetNode* UIWorkspace::FindSelectedNode()
{
    return const_cast<UIWidgetNode*>(static_cast<const UIWorkspace*>(this)->FindSelectedNode());
}

const UIWidgetNode* UIWorkspace::FindSelectedNode() const
{
    return FindNodeById(m_SelectedNodeId);
}

UIWidgetNode* UIWorkspace::FindNodeById(const std::string& id)
{
    return const_cast<UIWidgetNode*>(static_cast<const UIWorkspace*>(this)->FindNodeById(id));
}

const UIWidgetNode* UIWorkspace::FindNodeById(const std::string& id) const
{
    if (id.empty())
        return nullptr;

    EnsureNodeCache();
    const auto it = m_NodeCache.find(id);
    return it != m_NodeCache.end() ? it->second.node : nullptr;
}

UIWidgetNode* UIWorkspace::FindNodeByIdRecursive(UIWidgetNode& node, const std::string& id)
{
    if (node.id == id)
        return &node;
    for (UIWidgetNode& child : node.children)
    {
        if (UIWidgetNode* found = FindNodeByIdRecursive(child, id))
            return found;
    }
    return nullptr;
}

const UIWidgetNode* UIWorkspace::FindNodeByIdRecursive(const UIWidgetNode& node, const std::string& id) const
{
    if (node.id == id)
        return &node;
    for (const UIWidgetNode& child : node.children)
    {
        if (const UIWidgetNode* found = FindNodeByIdRecursive(child, id))
            return found;
    }
    return nullptr;
}

bool UIWorkspace::RemoveNodeById(UIWidgetNode& node, const std::string& id)
{
    for (auto it = node.children.begin(); it != node.children.end(); ++it)
    {
        if (it->id == id)
        {
            node.children.erase(it);
            return true;
        }
        if (RemoveNodeById(*it, id))
            return true;
    }
    return false;
}

std::string UIWorkspace::MakeUniqueNodeId(const std::string& base) const
{
    return m_Document->MakeUniqueWidgetId(base);
}

UIWidgetNode UIWorkspace::MakeNodeTemplate(const char* label) const
{
    UIWidgetNode node = ::Dot::MakeWidgetTemplate(label);
    node.id = MakeUniqueNodeId(node.id);
    node.name = node.id;
    return node;
}

void UIWorkspace::AddNodeToSelection(UIWidgetNode node)
{
    UIWidgetNode* parent = FindSelectedNode();
    if (!parent)
        parent = &m_Document->GetRoot();
    if (parent->id == node.id)
        node.id = MakeUniqueNodeId(node.id + "_Child");

    node.layout.offsetX = 32.0f + (static_cast<float>(parent->children.size()) * 22.0f);
    node.layout.offsetY = 32.0f + (static_cast<float>(parent->children.size()) * 22.0f);
    parent->children.push_back(std::move(node));
    m_Document->MarkDirty();
    m_SelectedNodeId = parent->children.back().id;
}

void UIWorkspace::SelectNode(const std::string& id)
{
    if (id.empty())
        return;
    m_SelectedNodeId = id;
}

void UIWorkspace::DeleteSelectedNode()
{
    if (m_SelectedNodeId.empty() || m_SelectedNodeId == m_Document->GetRoot().id)
        return;

    const UIWidgetNode* parent = FindParentOfNode(m_SelectedNodeId);
    if (RemoveNodeById(m_Document->GetRoot(), m_SelectedNodeId))
    {
        m_Document->MarkDirty();
        m_SelectedNodeId = parent ? parent->id : m_Document->GetRoot().id;
    }
}

void UIWorkspace::SelectParentNode()
{
    UIWidgetNode* selected = FindSelectedNode();
    if (!selected || selected->id == m_Document->GetRoot().id)
        return;

    if (const UIWidgetNode* parent = FindParentOfNode(selected->id))
        m_SelectedNodeId = parent->id;
}

void UIWorkspace::DuplicateSelectedNode()
{
    const UIWidgetNode* selected = FindSelectedNode();
    if (!selected || selected->id == m_Document->GetRoot().id)
        return;

    UIWidgetNode copy = *selected;
    std::function<void(UIWidgetNode&)> remapIds = [&](UIWidgetNode& node)
    {
        node.id = MakeUniqueNodeId(node.id + "_Copy");
        if (node.name.empty())
            node.name = node.id;
        for (UIWidgetNode& child : node.children)
            remapIds(child);
    };
    remapIds(copy);
    copy.layout.offsetX += 24.0f;
    copy.layout.offsetY += 24.0f;

    std::function<bool(UIWidgetNode&)> insertCopy = [&](UIWidgetNode& node) -> bool
    {
        for (auto it = node.children.begin(); it != node.children.end(); ++it)
        {
            if (it->id == selected->id)
            {
                node.children.insert(it + 1, copy);
                return true;
            }
            if (insertCopy(*it))
                return true;
        }
        return false;
    };

    if (!insertCopy(m_Document->GetRoot()))
        m_Document->GetRoot().children.push_back(copy);
    m_Document->MarkDirty();
    m_SelectedNodeId = copy.id;
}

bool UIWorkspace::ExtractNodeById(UIWidgetNode& node, const std::string& id, UIWidgetNode& outNode)
{
    for (auto it = node.children.begin(); it != node.children.end(); ++it)
    {
        if (it->id == id)
        {
            outNode = std::move(*it);
            node.children.erase(it);
            return true;
        }

        if (ExtractNodeById(*it, id, outNode))
            return true;
    }

    return false;
}

bool UIWorkspace::InsertNodeAsChild(const std::string& parentId, UIWidgetNode node)
{
    UIWidgetNode* parent = FindNodeById(parentId);
    if (!parent)
        return false;

    parent->children.push_back(std::move(node));
    return true;
}

bool UIWorkspace::InsertNodeAfter(const std::string& targetId, UIWidgetNode node)
{
    if (targetId == m_Document->GetRoot().id)
        return false;

    UIWidgetNode* parent = FindParentOfNode(targetId);
    if (!parent)
        parent = &m_Document->GetRoot();

    auto it = std::find_if(parent->children.begin(), parent->children.end(),
                           [&](const UIWidgetNode& child) { return child.id == targetId; });
    if (it == parent->children.end())
        return false;

    parent->children.insert(it + 1, std::move(node));
    return true;
}

bool UIWorkspace::NodeSubtreeContainsId(const UIWidgetNode& node, const std::string& id) const
{
    if (node.id == id)
        return true;

    for (const UIWidgetNode& child : node.children)
    {
        if (NodeSubtreeContainsId(child, id))
            return true;
    }

    return false;
}

bool UIWorkspace::MoveNodeToParent(const std::string& nodeId, const std::string& parentId)
{
    if (nodeId.empty() || parentId.empty() || nodeId == parentId || nodeId == m_Document->GetRoot().id)
        return false;

    const UIWidgetNode* draggedNode = FindNodeById(nodeId);
    const UIWidgetNode* targetParent = FindNodeById(parentId);
    if (!draggedNode || !targetParent)
        return false;
    if (NodeSubtreeContainsId(*draggedNode, parentId))
        return false;

    const UIWidgetNode* currentParent = FindParentOfNode(nodeId);
    if (currentParent && currentParent->id == parentId)
        return false;

    UIWidgetNode extracted;
    if (!ExtractNodeById(m_Document->GetRoot(), nodeId, extracted))
        return false;
    if (!InsertNodeAsChild(parentId, std::move(extracted)))
        return false;

    m_Document->MarkDirty();
    return true;
}

bool UIWorkspace::MoveNodeAfter(const std::string& nodeId, const std::string& targetId)
{
    if (nodeId.empty() || targetId.empty() || nodeId == targetId || nodeId == m_Document->GetRoot().id ||
        targetId == m_Document->GetRoot().id)
        return false;

    const UIWidgetNode* draggedNode = FindNodeById(nodeId);
    const UIWidgetNode* targetNode = FindNodeById(targetId);
    if (!draggedNode || !targetNode)
        return false;
    if (NodeSubtreeContainsId(*draggedNode, targetId))
        return false;

    const UIWidgetNode* currentParent = FindParentOfNode(nodeId);
    const UIWidgetNode* targetParent = FindParentOfNode(targetId);
    if (currentParent && targetParent && currentParent->id == targetParent->id)
    {
        const UIWidgetNode* currentTarget = FindNodeById(targetId);
        if (currentTarget && currentTarget->id == nodeId)
            return false;
    }

    UIWidgetNode extracted;
    if (!ExtractNodeById(m_Document->GetRoot(), nodeId, extracted))
        return false;
    if (!InsertNodeAfter(targetId, std::move(extracted)))
        return false;

    m_Document->MarkDirty();
    return true;
}

ImVec2 UIWorkspace::ComputeNodeMin(const UIWidgetNode& node, const ImVec2& parentMin, const ImVec2& parentMax)
{
    const ImVec2 parentSize(parentMax.x - parentMin.x, parentMax.y - parentMin.y);
    const ImVec2 anchorMin(parentMin.x + parentSize.x * node.layout.anchorMinX, parentMin.y + parentSize.y * node.layout.anchorMinY);
    ImVec2 min = ImVec2(anchorMin.x + node.layout.offsetX, anchorMin.y + node.layout.offsetY);
    if (node.layout.anchorMinX == node.layout.anchorMaxX && node.layout.anchorMinY == node.layout.anchorMaxY)
    {
        const ImVec2 size(node.layout.sizeX, node.layout.sizeY);
        min = ImVec2(min.x - size.x * node.layout.alignX, min.y - size.y * node.layout.alignY);
    }
    return min;
}

ImVec2 UIWorkspace::ComputeNodeMax(const UIWidgetNode& node, const ImVec2& parentMin, const ImVec2& parentMax)
{
    const ImVec2 parentSize(parentMax.x - parentMin.x, parentMax.y - parentMin.y);
    const ImVec2 anchorMax(parentMin.x + parentSize.x * node.layout.anchorMaxX, parentMin.y + parentSize.y * node.layout.anchorMaxY);
    const ImVec2 min = ComputeNodeMin(node, parentMin, parentMax);
    if (node.layout.anchorMinX == node.layout.anchorMaxX && node.layout.anchorMinY == node.layout.anchorMaxY)
        return ImVec2(min.x + node.layout.sizeX, min.y + node.layout.sizeY);
    return ImVec2(anchorMax.x + node.layout.offsetX + node.layout.sizeX, anchorMax.y + node.layout.offsetY + node.layout.sizeY);
}

void UIWorkspace::DrawInspectorForSelectedNode()
{
    UIWidgetNode* selected = FindSelectedNode();
    if (!selected)
    {
        ImGui::TextDisabled("No widget selected");
        return;
    }

    if (selected == &m_Document->GetRoot())
        ImGui::TextColored(ImVec4(0.55f, 0.75f, 0.95f, 1.0f), "Root Widget");
    else
        ImGui::TextColored(ImVec4(0.55f, 0.75f, 0.95f, 1.0f), "%s", selected->id.c_str());

    const std::string selectedPath = GetSelectedNodePath();
    if (!selectedPath.empty())
        ImGui::TextDisabled("%s", selectedPath.c_str());

    const float actionSpacing = ImGui::GetStyle().ItemSpacing.x;
    const float halfActionWidth = (ImGui::GetContentRegionAvail().x - actionSpacing) * 0.5f;
    if (selected != &m_Document->GetRoot())
    {
        if (DrawHalfWidthButton("Select Parent", halfActionWidth))
            SelectParentNode();
        ImGui::SameLine();
    }
    if (DrawHalfWidthButton("Add Child", halfActionWidth))
        AddNodeToSelection(MakeNodeTemplate("Panel"));
    if (selected == &m_Document->GetRoot())
    {
        ImGui::SameLine();
        ImGui::Dummy(ImVec2(halfActionWidth, 0.0f));
    }
    else if (DrawHalfWidthButton("Add Sibling", halfActionWidth))
        AddNodeAsSiblingToSelection(MakeNodeTemplate("Panel"));
    if (selected != &m_Document->GetRoot())
        ImGui::SameLine();
    else
        ImGui::NewLine();
    if (DrawHalfWidthButton("Duplicate", halfActionWidth))
        DuplicateSelectedNode();
    ImGui::SameLine();
    if (DrawHalfWidthButton("Delete", halfActionWidth))
        DeleteSelectedNode();

    ImGui::Separator();

    EnsureDocumentSuggestionCache();

    if (ImGui::BeginTabBar("##UIInspectorTabs"))
    {
        if (ImGui::BeginTabItem("Asset"))
        {
            char idBuffer[256] = {};
            strncpy_s(idBuffer, selected->id.c_str(), _TRUNCATE);
            if (ImGui::InputText("Id", idBuffer, sizeof(idBuffer)))
            {
                selected->id = idBuffer;
                m_Document->MarkDirty();
            }

            char nameBuffer[256] = {};
            strncpy_s(nameBuffer, selected->name.c_str(), _TRUNCATE);
            if (ImGui::InputText("Name", nameBuffer, sizeof(nameBuffer)))
            {
                selected->name = nameBuffer;
                m_Document->MarkDirty();
            }

            int typeIndex = static_cast<int>(selected->type);
            const char* typeNames[] = {"Panel", "Text", "Image", "Button", "ProgressBar", "Spacer"};
            if (ImGui::Combo("Type", &typeIndex, typeNames, IM_ARRAYSIZE(typeNames)))
            {
                selected->type = static_cast<UIWidgetType>(std::clamp(typeIndex, 0, 5));
                m_Document->MarkDirty();
            }

            if (ImGui::Checkbox("Enabled", &selected->enabled))
                m_Document->MarkDirty();

            if (selected->type != UIWidgetType::Spacer && selected->type != UIWidgetType::Image)
            {
                char textBuffer[1024] = {};
                strncpy_s(textBuffer, selected->text.c_str(), _TRUNCATE);
                if (ImGui::InputTextMultiline("Text", textBuffer, sizeof(textBuffer), ImVec2(-1.0f, 80.0f)))
                {
                    selected->text = textBuffer;
                    m_Document->MarkDirty();
                }
            }
            if (DrawStringSuggestionCombo("Binding Key", selected->bindingKey, m_BindingSuggestionOptions))
                m_Document->MarkDirty();
            char bindingBuffer[256] = {};
            strncpy_s(bindingBuffer, selected->bindingKey.c_str(), _TRUNCATE);
            if (ImGui::InputText("Binding Key Text", bindingBuffer, sizeof(bindingBuffer)))
            {
                selected->bindingKey = bindingBuffer;
                m_Document->MarkDirty();
            }
            if (selected->type == UIWidgetType::ProgressBar &&
                ImGui::DragFloat("Progress", &selected->progress, 0.01f, 0.0f, 1.0f))
                m_Document->MarkDirty();
            if (DrawStringSuggestionCombo("On Click", selected->onClickEvent, m_EventSuggestionOptions))
                m_Document->MarkDirty();
            char clickBuffer[256] = {};
            strncpy_s(clickBuffer, selected->onClickEvent.c_str(), _TRUNCATE);
            if (ImGui::InputText("On Click Text", clickBuffer, sizeof(clickBuffer)))
            {
                selected->onClickEvent = clickBuffer;
                m_Document->MarkDirty();
            }
            if (DrawStringSuggestionCombo("On Change", selected->onChangeEvent, m_EventSuggestionOptions))
                m_Document->MarkDirty();
            char changeBuffer[256] = {};
            strncpy_s(changeBuffer, selected->onChangeEvent.c_str(), _TRUNCATE);
            if (ImGui::InputText("On Change Text", changeBuffer, sizeof(changeBuffer)))
            {
                selected->onChangeEvent = changeBuffer;
                m_Document->MarkDirty();
            }
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Layout"))
        {
            bool changed = false;
            const int currentAnchorPreset = FindAnchorPresetIndex(selected->layout);
            const char* anchorPreview = currentAnchorPreset >= 0 ? kAnchorPresets[currentAnchorPreset].name : "Custom";
            DrawLabeledCombo("Anchor Preset", "##UIAnchorPreset", anchorPreview,
                             [&]()
            {
                for (int i = 0; i < static_cast<int>(std::size(kAnchorPresets)); ++i)
                {
                    const bool isSelected = (i == currentAnchorPreset);
                    if (ImGui::Selectable(kAnchorPresets[i].name, isSelected))
                    {
                        ApplyAnchorPreset(selected->layout, kAnchorPresets[i]);
                        changed = true;
                    }
                    if (isSelected)
                        ImGui::SetItemDefaultFocus();
                }
            });

            const int currentAlignmentPreset = FindAlignmentPresetIndex(selected->layout);
            const char* alignmentPreview =
                currentAlignmentPreset >= 0 ? kAlignmentPresets[currentAlignmentPreset].name : "Custom";
            DrawLabeledCombo("Alignment Preset", "##UIAlignmentPreset", alignmentPreview,
                             [&]()
            {
                for (int i = 0; i < static_cast<int>(std::size(kAlignmentPresets)); ++i)
                {
                    const bool isSelected = (i == currentAlignmentPreset);
                    if (ImGui::Selectable(kAlignmentPresets[i].name, isSelected))
                    {
                        ApplyAlignmentPreset(selected->layout, kAlignmentPresets[i]);
                        changed = true;
                    }
                    if (isSelected)
                        ImGui::SetItemDefaultFocus();
                }
            });

            const float presetButtonSpacing = ImGui::GetStyle().ItemSpacing.x;
            const float presetButtonWidth = (ImGui::GetContentRegionAvail().x - (presetButtonSpacing * 2.0f)) / 3.0f;
            if (DrawHalfWidthButton("Fill Parent", presetButtonWidth))
            {
                ApplyAnchorPreset(selected->layout, kAnchorPresets[9]);
                selected->layout.sizeX = 0.0f;
                selected->layout.sizeY = 0.0f;
                changed = true;
            }
            ImGui::SameLine();
            if (DrawHalfWidthButton("Center", presetButtonWidth))
            {
                ApplyAnchorPreset(selected->layout, kAnchorPresets[4]);
                changed = true;
            }
            ImGui::SameLine();
            if (DrawHalfWidthButton("Reset Size", presetButtonWidth))
            {
                selected->layout.sizeX = 220.0f;
                selected->layout.sizeY = 48.0f;
                changed = true;
            }
            ImGui::Separator();
            changed |= DrawLabeledDragFloat2("Anchor Min", "##UIAnchorMinX", "##UIAnchorMinY",
                                             selected->layout.anchorMinX, selected->layout.anchorMinY, 0.01f, 0.0f, 1.0f);
            changed |= DrawLabeledDragFloat2("Anchor Max", "##UIAnchorMaxX", "##UIAnchorMaxY",
                                             selected->layout.anchorMaxX, selected->layout.anchorMaxY, 0.01f, 0.0f, 1.0f);
            changed |= DrawLabeledDragFloat2("Offset", "##UIOffsetX", "##UIOffsetY",
                                             selected->layout.offsetX, selected->layout.offsetY, 1.0f, 0.0f, 0.0f);
            changed |= DrawLabeledDragFloat2("Size", "##UISizeX", "##UISizeY",
                                             selected->layout.sizeX, selected->layout.sizeY, 1.0f, 1.0f, 4096.0f);
            changed |= DrawLabeledDragFloat2("Align", "##UIAlignX", "##UIAlignY",
                                             selected->layout.alignX, selected->layout.alignY, 0.01f, 0.0f, 1.0f);
            changed |= DrawLabeledDragInt("Z Order", "##UIZOrder", selected->layout.zOrder, 1.0f);
            changed |= DrawLabeledCheckbox("Visible", "##UIVisible", selected->layout.visible);
            if (changed)
                m_Document->MarkDirty();
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Style"))
        {
            bool changed = false;
            changed |= ImGui::ColorEdit4("Background", &selected->style.backgroundR);
            changed |= ImGui::ColorEdit4("Text Color", &selected->style.textR);
            changed |= ImGui::ColorEdit4("Border Color", &selected->style.borderR);
            changed |= ImGui::DragFloat("Border Thickness", &selected->style.borderThickness, 0.05f, 0.0f, 12.0f);
            changed |= ImGui::DragFloat2("Padding", &selected->style.paddingX, 0.5f, 0.0f, 128.0f);
            changed |= ImGui::DragFloat("Opacity", &selected->style.opacity, 0.01f, 0.0f, 1.0f);
            FileFilter fontFilter{"Font Files", "*.ttf"};
            changed |= DrawAssetPicker("Font Path", "Browse##UIFontPath", fontFilter,
                                       [this]() -> const std::vector<std::string>& { return GetFontAssetOptions(); },
                                       selected->style.fontPath);

            if (selected->type == UIWidgetType::Image)
            {
                FileFilter imageFilter{"Image Files", "*.png"};
                changed |= DrawAssetPicker("Image Path", "Browse##UIImagePath", imageFilter,
                                           [this]() -> const std::vector<std::string>& { return GetImageAssetOptions(); },
                                           selected->style.imagePath);
            }

            if (selected->type == UIWidgetType::Panel || selected->type == UIWidgetType::Button || selected->type == UIWidgetType::Image)
            {
                FileFilter materialFilter{"Material Files", "*.dotmat"};
                changed |= DrawAssetPicker("Material Path", "Browse##UIMaterialPath", materialFilter,
                                           [this]() -> const std::vector<std::string>& { return GetMaterialAssetOptions(); },
                                           selected->style.materialPath);
            }
            if (changed)
                m_Document->MarkDirty();
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Events"))
        {
            bool changed = false;
            changed |= DrawStringSuggestionCombo("On Click", selected->onClickEvent, m_EventSuggestionOptions);
            changed |= DrawStringSuggestionCombo("On Change", selected->onChangeEvent, m_EventSuggestionOptions);
            changed |= DrawStringSuggestionCombo("Binding Key", selected->bindingKey, m_BindingSuggestionOptions);
            if (changed)
                m_Document->MarkDirty();
            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }
}

bool UIWorkspace::NodeMatchesHierarchyFilter(const UIWidgetNode& node, const std::string& filterLower) const
{
    if (filterLower.empty())
        return true;

    if (filterLower == "selected" && m_SelectedNodeId == node.id)
        return true;

    EnsureNodeCache();
    const auto it = m_NodeCache.find(node.id);
    return it != m_NodeCache.end() && it->second.searchText.find(filterLower) != std::string::npos;
}

bool UIWorkspace::IsNodeVisibleInHierarchy(const UIWidgetNode& node, const std::string* filterLower) const
{
    if (!filterLower || filterLower->empty())
        return true;
    return m_HierarchyVisibleNodeIds.find(node.id) != m_HierarchyVisibleNodeIds.end();
}

bool UIWorkspace::BuildNodePathRecursive(const UIWidgetNode& node, const std::string& targetId, std::vector<std::string>& path) const
{
    path.push_back(node.name.empty() ? node.id : node.name);
    if (node.id == targetId)
        return true;

    for (const UIWidgetNode& child : node.children)
    {
        if (BuildNodePathRecursive(child, targetId, path))
            return true;
    }

    path.pop_back();
    return false;
}

std::string UIWorkspace::GetSelectedNodePath() const
{
    if (m_SelectedNodeId.empty())
        return {};
    if (m_SelectedPathCacheRevision == m_Document->GetRevision() && m_SelectedPathCacheNodeId == m_SelectedNodeId)
        return m_SelectedPathCacheValue;

    EnsureNodeCache();
    const auto it = m_NodeCache.find(m_SelectedNodeId);
    if (it == m_NodeCache.end())
        return {};

    m_SelectedPathCacheRevision = m_Document->GetRevision();
    m_SelectedPathCacheNodeId = m_SelectedNodeId;
    m_SelectedPathCacheValue = it->second.path;
    return m_SelectedPathCacheValue;
}

UIWidgetNode* UIWorkspace::FindParentOfNode(const std::string& id)
{
    return const_cast<UIWidgetNode*>(static_cast<const UIWorkspace*>(this)->FindParentOfNode(id));
}

const UIWidgetNode* UIWorkspace::FindParentOfNode(const std::string& id) const
{
    if (id.empty() || id == m_Document->GetRoot().id)
        return nullptr;

    EnsureNodeCache();
    const auto it = m_NodeCache.find(id);
    return it != m_NodeCache.end() ? it->second.parent : nullptr;
}

UIWidgetNode* UIWorkspace::FindParentOfNodeRecursive(UIWidgetNode& node, const std::string& id)
{
    for (UIWidgetNode& child : node.children)
    {
        if (child.id == id)
            return &node;
        if (UIWidgetNode* found = FindParentOfNodeRecursive(child, id))
            return found;
    }
    return nullptr;
}

const UIWidgetNode* UIWorkspace::FindParentOfNodeRecursive(const UIWidgetNode& node, const std::string& id) const
{
    for (const UIWidgetNode& child : node.children)
    {
        if (child.id == id)
            return &node;
        if (const UIWidgetNode* found = FindParentOfNodeRecursive(child, id))
            return found;
    }
    return nullptr;
}

void UIWorkspace::AddNodeAsSiblingToSelection(UIWidgetNode node)
{
    UIWidgetNode* selected = FindSelectedNode();
    if (!selected || selected->id == m_Document->GetRoot().id)
    {
        AddNodeToSelection(std::move(node));
        return;
    }

    UIWidgetNode* parent = FindParentOfNode(selected->id);
    if (!parent)
        parent = &m_Document->GetRoot();

    if (parent->id == node.id)
        node.id = MakeUniqueNodeId(node.id + "_Sibling");

    node.layout.offsetX = selected->layout.offsetX + 24.0f;
    node.layout.offsetY = selected->layout.offsetY + 24.0f;
    const std::string insertedId = node.id;

    auto insertPos = std::find_if(parent->children.begin(), parent->children.end(),
                                  [&](const UIWidgetNode& child) { return child.id == selected->id; });
    if (insertPos != parent->children.end())
        parent->children.insert(insertPos + 1, std::move(node));
    else
        parent->children.push_back(std::move(node));

    m_Document->MarkDirty();
    m_SelectedNodeId = insertedId;
}

} // namespace Dot












