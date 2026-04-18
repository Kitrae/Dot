#pragma once

#include "EditorSelectionState.h"
#include "SceneSettingsAsset.h"

#include <string>

namespace Dot
{

class World;
class MapDocument;

class EditorSceneContext
{
public:
    void BindWorld(World* world) { m_World = world; }
    void BindMapDocuments(MapDocument* editingDocument, MapDocument* sceneDocument)
    {
        m_EditingMapDocument = editingDocument;
        m_SceneMapDocument = sceneDocument;
    }

    World* GetWorld() const { return m_World; }
    MapDocument* GetEditingMapDocument() const { return m_EditingMapDocument; }
    MapDocument* GetSceneMapDocument() const { return m_SceneMapDocument; }

    EditorSelectionState& GetEntitySelection() { return m_EntitySelection; }
    const EditorSelectionState& GetEntitySelection() const { return m_EntitySelection; }

    SceneSettingsAsset& GetSceneSettings() { return m_SceneSettings; }
    const SceneSettingsAsset& GetSceneSettings() const { return m_SceneSettings; }
    void ResetSceneSettings() { m_SceneSettings = SceneSettingsAsset{}; }

    const std::string& GetScenePath() const { return m_ScenePath; }
    void SetScenePath(std::string path) { m_ScenePath = std::move(path); }
    void ClearScenePath() { m_ScenePath.clear(); }

    bool IsSceneDirty() const { return m_SceneDirty; }
    void SetSceneDirty(bool dirty) { m_SceneDirty = dirty; }

    const std::string& GetLastSavedSceneSnapshot() const { return m_LastSavedSceneSnapshot; }
    void SetLastSavedSceneSnapshot(std::string snapshot) { m_LastSavedSceneSnapshot = std::move(snapshot); }

    float GetSceneDirtyCheckAccumulator() const { return m_SceneDirtyCheckAccumulator; }
    void SetSceneDirtyCheckAccumulator(float accumulator) { m_SceneDirtyCheckAccumulator = accumulator; }

    bool GetForceSceneDirtyCheck() const { return m_ForceSceneDirtyCheck; }
    void SetForceSceneDirtyCheck(bool forceDirtyCheck) { m_ForceSceneDirtyCheck = forceDirtyCheck; }

    void ResetTransientState()
    {
        m_EntitySelection.Clear();
        m_SceneDirty = false;
        m_SceneDirtyCheckAccumulator = 0.0f;
        m_ForceSceneDirtyCheck = false;
        m_LastSavedSceneSnapshot.clear();
    }

private:
    World* m_World = nullptr;
    MapDocument* m_EditingMapDocument = nullptr;
    MapDocument* m_SceneMapDocument = nullptr;
    EditorSelectionState m_EntitySelection;
    SceneSettingsAsset m_SceneSettings;
    std::string m_ScenePath;
    bool m_SceneDirty = false;
    std::string m_LastSavedSceneSnapshot;
    float m_SceneDirtyCheckAccumulator = 0.0f;
    bool m_ForceSceneDirtyCheck = false;
};

} // namespace Dot
