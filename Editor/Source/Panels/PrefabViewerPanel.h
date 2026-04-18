// =============================================================================
// Dot Engine - Prefab Viewer Panel
// =============================================================================
// View prefab structure and components in a read-only inspector.
// =============================================================================

#pragma once

#include "Core/Scene/Prefab.h"

#include "EditorPanel.h"

#include <filesystem>
#include <string>

namespace Dot
{

/// Prefab viewer panel - displays prefab entities and their components
class PrefabViewerPanel : public EditorPanel
{
public:
    PrefabViewerPanel();
    ~PrefabViewerPanel() override = default;

    void OnImGui() override;

    /// Open a prefab file for viewing
    void OpenPrefab(const std::filesystem::path& path);

    /// Close the current prefab
    void ClosePrefab();

    /// Check if a prefab is currently open
    bool HasOpenPrefab() const { return !m_FilePath.empty() && m_Prefab.IsValid(); }

    /// Get the currently open file path
    const std::filesystem::path& GetFilePath() const { return m_FilePath; }

private:
    void DrawEntityNode(size_t index, const PrefabEntity& entity);
    void DrawComponentData(const std::string& typeName, const std::string& jsonData);

    // Parse Vec3 from component JSON
    Vec3 ParseVec3(const std::string& jsonData, const std::string& key);
    float ParseFloat(const std::string& jsonData, const std::string& key);
    int ParseInt(const std::string& jsonData, const std::string& key);
    bool ParseBool(const std::string& jsonData, const std::string& key);
    std::string ParseString(const std::string& jsonData, const std::string& key);

    // File state
    std::filesystem::path m_FilePath;
    Prefab m_Prefab;

    // UI state
    int m_SelectedEntityIndex = -1;
};

} // namespace Dot
