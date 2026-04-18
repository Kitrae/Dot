#pragma once

struct ImGuiIO;

namespace Dot
{

struct Ray;
class ViewportPanel;

class MapViewportToolController
{
public:
    static bool HandleInput(ViewportPanel& panel, const Ray& ray, bool canInteract, bool showSelectionGizmo, ImGuiIO& io);
};

} // namespace Dot
