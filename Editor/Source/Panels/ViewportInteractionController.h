#pragma once

struct ImGuiIO;

namespace Dot
{

class ViewportPanel;
struct Ray;

class ViewportInteractionController
{
public:
    static void HandleInput(ViewportPanel& panel);

private:
    static void HandleLayoutInput(ViewportPanel& panel, const Ray& ray, bool canInteract, bool showSelectionGizmo,
                                  ImGuiIO& io);
};

} // namespace Dot
