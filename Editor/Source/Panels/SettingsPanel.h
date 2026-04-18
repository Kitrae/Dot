// =============================================================================
// Dot Engine - Settings Panel
// =============================================================================
// Standalone project settings window with category navigation.
// =============================================================================

#pragma once

namespace Dot
{

class SettingsPanel
{
public:
    enum class Category
    {
        Editor,
        Gizmos,
        View,
        Physics,
        CollisionLayers
    };

    static void Open(Category category = Category::Editor);

    static void OnImGui();

    static Category GetSelectedCategory();
    static void SetSelectedCategory(Category category);

private:
    static bool s_IsOpen;
    static Category s_SelectedCategory;
};

} // namespace Dot
