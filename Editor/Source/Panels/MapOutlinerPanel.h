#pragma once

#include "EditorPanel.h"

#include "../Map/MapDocument.h"

namespace Dot
{

class MapOutlinerPanel : public EditorPanel
{
public:
    MapOutlinerPanel() : EditorPanel("Map Outliner") {}

    void SetDocument(MapDocument* document) { m_Document = document; }
    void OnImGui() override;

private:
    MapDocument* m_Document = nullptr;
};

} // namespace Dot
