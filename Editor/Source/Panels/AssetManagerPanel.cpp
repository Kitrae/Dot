// =============================================================================
// Dot Engine - Asset Manager Panel Implementation
// =============================================================================

#include "AssetManagerPanel.h"
#include "PanelChrome.h"

#include "Core/Assets/AssetManager.h"

#include <algorithm>
#include <cctype>
#include <imgui.h>
#include <system_error>

#ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <shellapi.h>

namespace Dot
{

namespace
{

bool IsTextAssetExtension(const std::string& extension)
{
    return extension == ".txt" || extension == ".md" || extension == ".json" || extension == ".xml" ||
           extension == ".hlsl" || extension == ".glsl" || extension == ".ini" || extension == ".cfg" ||
           extension == ".lua";
}

bool IsTextureAssetExtension(const std::string& extension)
{
    return extension == ".png" || extension == ".jpg" || extension == ".jpeg" || extension == ".tga" ||
           extension == ".bmp";
}

void PushAssetToolbarButtonStyle()
{
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8.0f, 5.0f));
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.21f, 0.26f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.24f, 0.30f, 0.38f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.28f, 0.37f, 0.48f, 1.0f));
}

void PopAssetToolbarButtonStyle()
{
    ImGui::PopStyleColor(3);
    ImGui::PopStyleVar();
}

} // namespace

AssetManagerPanel::AssetManagerPanel() : EditorPanel("Assets")
{
    // Default to project root if not set
    m_RootPath = AssetManager::Get().GetRootPath();
    m_CurrentPath = m_RootPath;
}

AssetManagerPanel::~AssetManagerPanel()
{
    // TODO: Clean up thumbnail textures
    m_ThumbnailCache.clear();
}

void AssetManagerPanel::SetRootPath(const std::filesystem::path& root)
{
    m_RootPath = root;
    m_CurrentPath = root;
    Refresh();
}

void AssetManagerPanel::NavigateTo(const std::filesystem::path& path)
{
    if (std::filesystem::exists(path) && std::filesystem::is_directory(path))
    {
        m_CurrentPath = path;
        m_SelectedIndex = -1;
        Refresh();
    }
}

void AssetManagerPanel::Refresh()
{
    m_Entries.clear();

    if (!std::filesystem::exists(m_CurrentPath))
    {
        m_CurrentPath = m_RootPath;
    }

    try
    {
        for (const auto& entry : std::filesystem::directory_iterator(m_CurrentPath))
        {
            try
            {
                AssetEntry asset;
                asset.path = entry.path();
                asset.name = entry.path().filename().string();
                asset.extension = entry.path().extension().string();

                // These can throw for special files
                std::error_code ec;
                asset.isDirectory = entry.is_directory(ec);
                if (ec)
                    continue; // Skip problematic entries

                // Convert extension to lowercase for comparison
                std::transform(asset.extension.begin(), asset.extension.end(), asset.extension.begin(),
                               [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

                if (!asset.isDirectory)
                {
                    asset.fileSize = entry.file_size(ec);
                    if (ec)
                        asset.fileSize = 0; // Just set to 0 if we can't read size
                }

                // Skip hidden files (starting with .)
                if (!asset.name.empty() && asset.name[0] == '.')
                    continue;

                m_Entries.push_back(asset);
            }
            catch (const std::exception&)
            {
                // Skip any problematic entries
                continue;
            }
        }

        // Sort based on selected option
        std::sort(m_Entries.begin(), m_Entries.end(),
                  [this](const AssetEntry& a, const AssetEntry& b)
                  {
                      // Always put directories first? Or respect sort?
                      // Usually file explorers put directories on top unless sorting by something specific.
                      // Let's keep directories on top for Name and Type sort.

                      bool aIsDir = a.isDirectory;
                      bool bIsDir = b.isDirectory;

                      if (m_SortOption == SortOption::Name || m_SortOption == SortOption::Type)
                      {
                          if (aIsDir != bIsDir)
                              return aIsDir > bIsDir; // Directories first
                      }

                      // Compare based on option
                      int comparison = 0;
                      switch (m_SortOption)
                      {
                          case SortOption::Name:
                              comparison = a.name.compare(b.name);
                              break;
                          case SortOption::Size:
                              if (a.fileSize < b.fileSize)
                                  comparison = -1;
                              else if (a.fileSize > b.fileSize)
                                  comparison = 1;
                              break;
                          case SortOption::Type:
                              // Group by extension
                              if (a.extension != b.extension)
                                  comparison = a.extension.compare(b.extension);
                              else
                                  comparison = a.name.compare(b.name);
                              break;
                          default:
                              break;
                      }

                      // Apply ascending/descending
                      return m_SortAscending ? (comparison < 0) : (comparison > 0);
                  });
    }
    catch (const std::exception& e)
    {
        // Log error but don't crash
        (void)e;
    }
}

void AssetManagerPanel::OnImGui()
{
    if (!m_Open)
        return;

    BeginChromeWindow(m_Name.c_str(), &m_Open, ImGuiWindowFlags_MenuBar);

    DrawToolbar();
    DrawBreadcrumbs();

    ImGui::Separator();

    // Main content area
    float footerHeight = ImGui::GetFrameHeightWithSpacing();
    ImGui::BeginChild("AssetContent", ImVec2(0, -footerHeight), false);

    if (m_ViewMode == AssetViewMode::Grid)
        DrawAssetGrid();
    else
        DrawAssetList();

    ImGui::EndChild();

    // Context menu for empty space
    if (ImGui::BeginPopupContextWindow("AssetContextEmpty",
                                       ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems))
    {
        if (ImGui::MenuItem("\xef\x81\xbb  New Folder"))
        {
            m_ShowCreateFolder = true;
            memset(m_NewFolderName, 0, sizeof(m_NewFolderName));
        }
        if (ImGui::MenuItem("\xef\x80\xa1  Refresh"))
        {
            Refresh();
        }
        ImGui::Separator();
        if (ImGui::MenuItem("\xef\x81\xbc  Open in Explorer"))
        {
            ShellExecuteW(nullptr, L"explore", m_CurrentPath.wstring().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        }
        ImGui::EndPopup();
    }

    DrawStatusBar();

    // Create folder popup
    if (m_ShowCreateFolder)
    {
        ImGui::OpenPopup("Create Folder");
    }
    if (ImGui::BeginPopupModal("Create Folder", &m_ShowCreateFolder, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::Text("Folder Name:");
        ImGui::SetNextItemWidth(200);
        bool create = ImGui::InputText("##foldername", m_NewFolderName, sizeof(m_NewFolderName),
                                       ImGuiInputTextFlags_EnterReturnsTrue);

        if (ImGui::Button("Create") || create)
        {
            if (m_NewFolderName[0] != '\0')
            {
                CreateFolder(m_NewFolderName);
                m_ShowCreateFolder = false;
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel"))
        {
            m_ShowCreateFolder = false;
        }
        ImGui::EndPopup();
    }

    ImGui::End();
}

void AssetManagerPanel::DrawToolbar()
{
    if (ImGui::BeginMenuBar())
    {
        PushAssetToolbarButtonStyle();

        // Back button
        bool canGoBack = m_CurrentPath != m_RootPath;
        if (!canGoBack)
            ImGui::BeginDisabled();
        if (ImGui::Button("\xef\x81\xa0")) // arrow-left
        {
            NavigateTo(m_CurrentPath.parent_path());
        }
        if (!canGoBack)
            ImGui::EndDisabled();

        ImGui::SameLine();

        // Home button
        if (ImGui::Button("\xef\x80\x95")) // home
        {
            NavigateTo(m_RootPath);
        }

        ImGui::SameLine();

        // Refresh button
        if (ImGui::Button("\xef\x80\xa1")) // refresh/sync
        {
            Refresh();
        }

        ImGui::SameLine();
        ImGui::TextDisabled("|");
        ImGui::SameLine();

        // Search Filter
        ImGui::SetNextItemWidth(180.0f);
        m_Filter.Draw("##Search");

        // Sort Options
        ImGui::SameLine();
        if (ImGui::Button("\xef\x81\x9e")) // sort-sort
        {
            ImGui::OpenPopup("SortOptions");
        }

        if (ImGui::BeginPopup("SortOptions"))
        {
            if (ImGui::MenuItem("Name", nullptr, m_SortOption == SortOption::Name))
            {
                m_SortOption = SortOption::Name;
                Refresh(); // Re-sort
            }
            if (ImGui::MenuItem("Type", nullptr, m_SortOption == SortOption::Type))
            {
                m_SortOption = SortOption::Type;
                Refresh();
            }
            if (ImGui::MenuItem("Size", nullptr, m_SortOption == SortOption::Size))
            {
                m_SortOption = SortOption::Size;
                Refresh();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Ascending", nullptr, m_SortAscending))
            {
                m_SortAscending = true;
                Refresh();
            }
            if (ImGui::MenuItem("Descending", nullptr, !m_SortAscending))
            {
                m_SortAscending = false;
                Refresh();
            }
            ImGui::EndPopup();
        }

        ImGui::SameLine();
        ImGui::TextDisabled("|");
        ImGui::SameLine();

        // View mode toggle
        if (ImGui::Button(m_ViewMode == AssetViewMode::Grid ? "\xef\x80\x8a" : "\xef\x80\x8b")) // th / list
        {
            m_ViewMode = (m_ViewMode == AssetViewMode::Grid) ? AssetViewMode::List : AssetViewMode::Grid;
        }

        // Thumbnail size slider (grid mode only)
        if (m_ViewMode == AssetViewMode::Grid)
        {
            ImGui::SameLine();
            ImGui::SetNextItemWidth(60);
            ImGui::SliderFloat("##size", &m_ThumbnailSize, 48.0f, 128.0f, "");
        }

        PopAssetToolbarButtonStyle();
        ImGui::EndMenuBar();
    }
}

void AssetManagerPanel::DrawBreadcrumbs()
{
    // Build path segments
    std::vector<std::filesystem::path> segments;
    std::filesystem::path current = m_CurrentPath;

    while (current != m_RootPath.parent_path() && !current.empty())
    {
        segments.push_back(current);
        if (current == m_RootPath)
            break;
        current = current.parent_path();
    }

    std::reverse(segments.begin(), segments.end());

    // Draw breadcrumb buttons
    for (size_t i = 0; i < segments.size(); ++i)
    {
        if (i > 0)
        {
            ImGui::SameLine(0, 2);
            ImGui::TextDisabled("/");
            ImGui::SameLine(0, 2);
        }

        std::string name = (i == 0) ? "Assets" : segments[i].filename().string();
        PushAssetToolbarButtonStyle();
        if (ImGui::Button(name.c_str()))
        {
            NavigateTo(segments[i]);
        }
        PopAssetToolbarButtonStyle();
    }
}

void AssetManagerPanel::DrawAssetGrid()
{
    float panelWidth = ImGui::GetContentRegionAvail().x;
    int columns = static_cast<int>(panelWidth / (m_ThumbnailSize + m_Padding));
    if (columns < 1)
        columns = 1;

    ImGui::Columns(columns, nullptr, false);

    for (int i = 0; i < static_cast<int>(m_Entries.size()); ++i)
    {
        AssetEntry& entry = m_Entries[i];

        // Apply search filter
        if (!m_Filter.PassFilter(entry.name.c_str()))
            continue;

        DrawAssetItem(entry, m_ThumbnailSize);

        // Handle double-click to open directories
        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0))
        {
            OpenAssetEntry(entry);
        }

        // Handle selection
        if (ImGui::IsItemClicked())
        {
            m_SelectedIndex = i;
        }

        // Context menu for item
        if (ImGui::BeginPopupContextItem())
        {
            if (entry.isDirectory)
            {
                if (ImGui::MenuItem("\xef\x81\xbc  Open"))
                {
                    NavigateTo(entry.path);
                }
            }
            else if (entry.extension == ".dotscene" && ImGui::MenuItem("\xef\x81\xbc  Open Scene"))
            {
                OpenAssetEntry(entry);
            }
            else if (entry.extension == ".dotmap" && ImGui::MenuItem("\xef\x81\xbc  Open Map"))
            {
                OpenAssetEntry(entry);
            }
            if (ImGui::MenuItem("\xef\x8e\x84  Rename"))
            {
                m_IsRenaming = true;
                m_RenamingIndex = i;
                strncpy(m_RenameBuffer, entry.name.c_str(), sizeof(m_RenameBuffer) - 1);
            }
            if (ImGui::MenuItem("\xef\x80\x8d  Delete"))
            {
                DeleteAsset(entry);
            }
            ImGui::Separator();
            if (ImGui::MenuItem("\xef\x81\xbc  Open in Explorer"))
            {
                // For files, select them in explorer. For folders, open them.
                if (entry.isDirectory)
                {
                    ShellExecuteW(nullptr, L"explore", entry.path.wstring().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
                }
                else
                {
                    // Open explorer and select the file
                    std::wstring cmd = L"/select,\"" + entry.path.wstring() + L"\"";
                    ShellExecuteW(nullptr, L"open", L"explorer.exe", cmd.c_str(), nullptr, SW_SHOWNORMAL);
                }
            }
            ImGui::EndPopup();
        }

        ImGui::NextColumn();
    }

    ImGui::Columns(1);
}

void AssetManagerPanel::DrawAssetList()
{
    ImGui::BeginChild("AssetList");
    ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.18f, 0.22f, 0.28f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.24f, 0.31f, 0.40f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.28f, 0.38f, 0.49f, 1.0f));

    for (int i = 0; i < static_cast<int>(m_Entries.size()); ++i)
    {
        AssetEntry& entry = m_Entries[i];
        AssetType type = entry.isDirectory ? AssetType::Folder : GetAssetType(entry.extension);
        const bool selected = (m_SelectedIndex == i);
        std::string rowLabel = std::string(GetAssetIcon(type)) + "  " + entry.name;
        ImGui::Selectable(rowLabel.c_str(), selected);

        if (ImGui::IsItemClicked())
        {
            m_SelectedIndex = i;
        }

        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0))
        {
            OpenAssetEntry(entry);
        }

        // Context menu
        if (ImGui::BeginPopupContextItem())
        {
            if (entry.isDirectory && ImGui::MenuItem("Open"))
            {
                NavigateTo(entry.path);
            }
            else if (entry.extension == ".dotscene" && ImGui::MenuItem("Open Scene"))
            {
                OpenAssetEntry(entry);
            }
            else if (entry.extension == ".dotmap" && ImGui::MenuItem("Open Map"))
            {
                OpenAssetEntry(entry);
            }
            if (ImGui::MenuItem("Rename"))
            {
                m_IsRenaming = true;
                m_RenamingIndex = i;
                strncpy(m_RenameBuffer, entry.name.c_str(), sizeof(m_RenameBuffer) - 1);
            }
            if (ImGui::MenuItem("Delete"))
            {
                DeleteAsset(entry);
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Open in Explorer"))
            {
                if (entry.isDirectory)
                {
                    ShellExecuteW(nullptr, L"explore", entry.path.wstring().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
                }
                else
                {
                    std::wstring cmd = L"/select,\"" + entry.path.wstring() + L"\"";
                    ShellExecuteW(nullptr, L"open", L"explorer.exe", cmd.c_str(), nullptr, SW_SHOWNORMAL);
                }
            }
            ImGui::EndPopup();
        }
    }

    ImGui::PopStyleColor(3);
    ImGui::EndChild();
}

void AssetManagerPanel::DrawAssetItem(AssetEntry& entry, float size)
{
    AssetType type = entry.isDirectory ? AssetType::Folder : GetAssetType(entry.extension);

    ImGui::PushID(entry.name.c_str());

    // Selection highlight
    ImVec2 pos = ImGui::GetCursorScreenPos();
    bool selected = (m_SelectedIndex >= 0 && m_SelectedIndex < static_cast<int>(m_Entries.size()) &&
                     m_Entries[m_SelectedIndex].path == entry.path);

    const bool hovered = ImGui::IsMouseHoveringRect(pos, ImVec2(pos.x + size, pos.y + size + 20));
    const ImU32 bgColor = selected ? IM_COL32(56, 84, 118, 220)
                                   : hovered ? IM_COL32(38, 48, 62, 160) : IM_COL32(24, 29, 36, 110);
    ImGui::GetWindowDrawList()->AddRectFilled(pos, ImVec2(pos.x + size, pos.y + size + 20), bgColor, 6.0f);
    ImGui::GetWindowDrawList()->AddRect(pos, ImVec2(pos.x + size, pos.y + size + 20), IM_COL32(68, 82, 100, 140),
                                        6.0f);

    ImGui::BeginGroup();

    // Thumbnail or icon
    if (type == AssetType::Texture && entry.thumbnailSRV)
    {
        // Show texture thumbnail
        ImGui::Image(entry.thumbnailSRV, ImVec2(size, size));
    }
    else
    {
        // Show icon centered
        ImGui::PushFont(nullptr); // Use default font for icons
        const char* icon = GetAssetIcon(type);

        // Center icon in box
        ImVec2 iconSize = ImGui::CalcTextSize(icon);
        float offsetX = (size - iconSize.x) * 0.5f;
        float offsetY = (size - iconSize.y) * 0.5f;

        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + offsetX);
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + offsetY);

        // Color based on type
        ImVec4 color = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
        if (type == AssetType::Folder)
            color = ImVec4(1.0f, 0.8f, 0.3f, 1.0f);
        else if (type == AssetType::Texture)
            color = ImVec4(0.5f, 0.8f, 1.0f, 1.0f);
        else if (type == AssetType::Model)
            color = ImVec4(0.8f, 0.5f, 1.0f, 1.0f);

        ImGui::TextColored(color, "%s", icon);
        ImGui::PopFont();

        // Reset cursor for name
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + size - offsetY - iconSize.y - 4);
    }

    // Asset name (truncated)
    std::string displayName = entry.name;
    if (displayName.length() > 12)
    {
        displayName = displayName.substr(0, 10) + "...";
    }

    // Center name
    float textWidth = ImGui::CalcTextSize(displayName.c_str()).x;
    float textOffset = (size - textWidth) * 0.5f;
    if (textOffset > 0)
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + textOffset);

    ImGui::TextUnformatted(displayName.c_str());

    ImGui::EndGroup();

    // Make the whole item clickable
    ImGui::SetCursorScreenPos(pos);
    ImGui::InvisibleButton("##asset", ImVec2(size, size + 20));

    // Tooltip with full name
    if (ImGui::IsItemHovered())
    {
        ImGui::BeginTooltip();
        ImGui::Text("%s", entry.name.c_str());
        if (!entry.isDirectory)
        {
            ImGui::TextDisabled("%.2f KB", entry.fileSize / 1024.0f);
        }
        ImGui::EndTooltip();
    }

    // Drag source for texture files
    if (!entry.isDirectory && (entry.extension == ".png" || entry.extension == ".jpg" || entry.extension == ".jpeg" ||
                               entry.extension == ".tga" || entry.extension == ".bmp"))
    {
        if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
        {
            std::string pathStr = entry.path.string();
            ImGui::SetDragDropPayload("TEXTURE_ASSET", pathStr.c_str(), pathStr.size() + 1);
            ImGui::Text("Texture: %s", entry.name.c_str());
            if (entry.thumbnailSRV)
            {
                ImGui::Image(entry.thumbnailSRV, ImVec2(32, 32));
            }
            ImGui::EndDragDropSource();
        }
    }

    // Drag source for prefab files
    if (!entry.isDirectory && entry.extension == ".prefab")
    {
        if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
        {
            std::string pathStr = entry.path.string();
            ImGui::SetDragDropPayload("PREFAB_ASSET", pathStr.c_str(), pathStr.size() + 1);
            ImGui::Text("Instantiate: %s", entry.name.c_str());
            ImGui::EndDragDropSource();
        }

        // Double-click to open in prefab viewer
        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0))
        {
            if (m_OnOpenPrefabFile)
            {
                m_OnOpenPrefabFile(entry.path);
            }
        }
    }

    // Drag source for model files
    if (!entry.isDirectory && (entry.extension == ".fbx" || entry.extension == ".obj" || entry.extension == ".gltf" ||
                               entry.extension == ".glb"))
    {
        if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
        {
            std::string pathStr = entry.path.string();
            ImGui::SetDragDropPayload("MODEL_ASSET", pathStr.c_str(), pathStr.size() + 1);
            ImGui::Text("Import Model: %s", entry.name.c_str());
            ImGui::EndDragDropSource();
        }
    }

    ImGui::PopID();
}

void AssetManagerPanel::OpenAssetEntry(const AssetEntry& entry)
{
    if (entry.isDirectory)
    {
        NavigateTo(entry.path);
        return;
    }

    if (entry.extension == ".dotscene")
    {
        if (m_OnOpenSceneFile)
            m_OnOpenSceneFile(entry.path);
        return;
    }

    if (entry.extension == ".dotmap")
    {
        if (m_OnOpenMapFile)
            m_OnOpenMapFile(entry.path);
        return;
    }

    if (entry.extension == ".dotui")
    {
        if (m_OnOpenUiFile)
            m_OnOpenUiFile(entry.path);
        return;
    }

    if (IsTextAssetExtension(entry.extension))
    {
        if (m_OnOpenTextFile)
            m_OnOpenTextFile(entry.path);
        return;
    }

    if (IsTextureAssetExtension(entry.extension))
    {
        if (m_OnOpenTextureFile)
            m_OnOpenTextureFile(entry.path);
        return;
    }

    if (entry.extension == ".prefab")
    {
        if (m_OnOpenPrefabFile)
            m_OnOpenPrefabFile(entry.path);
    }
}

void AssetManagerPanel::DrawStatusBar()
{
    ImGui::Separator();

    // Item count
    ImGui::TextDisabled("LIBRARY");
    ImGui::SameLine();
    ImGui::Text("%zu items", m_Entries.size());

    // Selected item info
    if (m_SelectedIndex >= 0 && m_SelectedIndex < static_cast<int>(m_Entries.size()))
    {
        ImGui::SameLine();
        ImGui::TextDisabled("|");
        ImGui::SameLine();
        ImGui::Text("Selected: %s", m_Entries[m_SelectedIndex].name.c_str());
    }
}

void AssetManagerPanel::LoadThumbnail(AssetEntry& entry)
{
    // TODO: Implement texture loading for thumbnails
    // This would use stb_image + D3D12 texture creation
    entry.thumbnailFailed = true; // For now, mark as failed
}

void AssetManagerPanel::CreateFolder(const std::string& name)
{
    std::filesystem::path newPath = m_CurrentPath / name;
    try
    {
        std::filesystem::create_directory(newPath);
        Refresh();
    }
    catch (const std::exception& e)
    {
        (void)e;
        // TODO: Show error
    }
}

void AssetManagerPanel::DeleteAsset(const AssetEntry& entry)
{
    try
    {
        if (entry.isDirectory)
        {
            std::filesystem::remove_all(entry.path);
        }
        else
        {
            std::filesystem::remove(entry.path);
        }
        Refresh();
    }
    catch (const std::exception& e)
    {
        (void)e;
        // TODO: Show error
    }
}

void AssetManagerPanel::RenameAsset(AssetEntry& entry, const std::string& newName)
{
    std::filesystem::path newPath = entry.path.parent_path() / newName;
    try
    {
        std::filesystem::rename(entry.path, newPath);
        Refresh();
    }
    catch (const std::exception& e)
    {
        (void)e;
        // TODO: Show error
    }
}

} // namespace Dot
