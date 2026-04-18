#pragma once

#include "Workspace.h"

#include <functional>
#include <string>

namespace Dot
{

class AssetManagerPanel;
class MapInspectorPanel;
class MapOutlinerPanel;
class MapDocument;
class ViewportPanel;
class EditorSceneContext;

class MapWorkspace : public Workspace
{
public:
    MapWorkspace();

    void SetPanels(MapOutlinerPanel* outliner, MapInspectorPanel* inspector, ViewportPanel* viewport,
                   AssetManagerPanel* assetManager);
    void SetSceneContext(EditorSceneContext* sceneContext) { m_SceneContext = sceneContext; }
    void SetDocument(MapDocument* document);
    void SetSaveMapCallback(std::function<bool()> callback) { m_SaveMapCallback = std::move(callback); }

    void OnActivate() override;
    void OnDeactivate() override;
    void OnImGui() override;

private:
    enum class ToolGlyph
    {
        Brush,
        Edge,
        Vertex,
        Clip,
        Extrude,
        Hollow,
        AddBrush,
        Save
    };

    enum class ToolbarTool
    {
        Select,
        Edge,
        Vertex,
        Clip,
        Extrude
    };

    void DrawToolbar();
    void DrawToolSettings();
    void DrawClipToolSettings();
    void DrawExtrudeToolSettings();
    void DrawHollowBoxToolSettings();
    void HandleToolKeyboardShortcuts();
    void BeginClipTool();
    void BeginExtrudeTool();
    void ResetClipToolPreview();
    void ResetExtrudeToolPreview();
    bool DrawToolButton(const char* id, const char* shortLabel, const char* title, const char* description,
                        const char* shortcut, bool active, bool enabled = true, ToolGlyph glyph = ToolGlyph::Brush);

    MapOutlinerPanel* m_OutlinerPanel = nullptr;
    MapInspectorPanel* m_InspectorPanel = nullptr;
    ViewportPanel* m_ViewportPanel = nullptr;
    AssetManagerPanel* m_AssetManagerPanel = nullptr;
    EditorSceneContext* m_SceneContext = nullptr;
    MapDocument* m_Document = nullptr;
    std::function<bool()> m_SaveMapCallback;
    std::string m_SaveStatus;
    bool m_SaveStatusIsError = false;
    ToolbarTool m_ActiveTool = ToolbarTool::Select;
    float m_ClipOffset = -0.25f;
    bool m_ClipFlipPlane = false;
    float m_ClipBaseOffset = -0.25f;
    bool m_ClipBaseFlipPlane = false;
    float m_ExtrudeDistance = 0.5f;
    float m_ExtrudeBaseDistance = 0.5f;
    bool m_ShowHollowBoxSettings = false;
    float m_HollowWallThickness = 0.25f;
};

} // namespace Dot
