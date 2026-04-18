#pragma once

#include "EditorPanel.h"

namespace Dot
{

class EditorSceneContext;

class SceneSettingsPanel : public EditorPanel
{
public:
    SceneSettingsPanel() : EditorPanel("Scene Settings")
    {
        SetOpen(false);
    }

    void SetSceneContext(EditorSceneContext* sceneContext) { m_SceneContext = sceneContext; }
    void OnImGui() override;

private:
    EditorSceneContext* m_SceneContext = nullptr;
};

} // namespace Dot
