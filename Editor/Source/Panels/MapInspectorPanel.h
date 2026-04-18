#pragma once

#include "EditorPanel.h"

#include "../Map/MapDocument.h"

#include <array>
#include <string>
#include <vector>

namespace Dot
{

class MapInspectorPanel : public EditorPanel
{
public:
    MapInspectorPanel() : EditorPanel("Map Inspector") {}

    void SetDocument(MapDocument* document) { m_Document = document; }
    void OnImGui() override;

private:
    void SyncUiState(bool force = false);
    bool DrawHalfWidthButton(const char* label, bool sameLine = false);
    void PushRecentMaterial(const std::string& materialPath);

    MapDocument* m_Document = nullptr;
    MapSelection m_LastSelection;
    std::vector<MapSelection> m_LastSelections;
    uint64 m_LastRevision = 0;
    std::array<char, 256> m_BrushNameBuffer = {};
    std::array<char, 260> m_MaterialBuffer = {};
    float m_BrushDelta[3] = {0.0f, 0.0f, 0.0f};
    float m_FaceDistance = 1.0f;
    float m_EdgeDelta[3] = {0.0f, 0.0f, 0.0f};
    float m_VertexDelta[3] = {0.0f, 0.0f, 0.0f};
    MapFaceUV m_FaceUv = {};
    float m_UvNudgeStep = 0.25f;
    std::vector<std::string> m_RecentMaterials;
    std::string m_SurfaceClipboardMaterial;
    MapFaceUV m_SurfaceClipboardUv = {};
    bool m_SurfaceClipboardValid = false;
};

} // namespace Dot
