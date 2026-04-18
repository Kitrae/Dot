// =============================================================================
// Dot Engine - Editor Settings
// =============================================================================
// Singleton for all editor preferences. Scalable for future features.
// =============================================================================

#pragma once

namespace Dot
{

/// Singleton holding all editor settings/preferences
class EditorSettings
{
public:
    static EditorSettings& Get()
    {
        static EditorSettings instance;
        return instance;
    }

    // Grid settings
    bool showGrid = true;
    float gridSize = 10.0f;   // Total size (extends from -size/2 to +size/2)
    float gridSpacing = 1.0f; // Space between lines
    float gridColorR = 0.3f;
    float gridColorG = 0.3f;
    float gridColorB = 0.3f;
    float gridColorA = 0.5f;

    // Axis indicator at origin
    bool showAxisIndicator = true;
    float axisLength = 1.0f;

    // Viewport gizmo overlays
    bool showSelectionGizmo = true;
    bool showLightGizmos = true;
    bool showCameraFrustums = true;
    bool showAttachmentSockets = false;
    bool showNavMeshGizmo = false;

    // Future settings can be added here:
    bool layoutTranslationSnapEnabled = false;
    float layoutTranslationSnapStep = 1.0f;
    bool layoutRotationSnapEnabled = false;
    float layoutRotationSnapStep = 15.0f;
    bool layoutScaleSnapEnabled = false;
    float layoutScaleSnapStep = 0.1f;
    bool mapTranslationSnapEnabled = true;
    float mapTranslationSnapStep = 1.0f;

private:
    EditorSettings() = default;
    EditorSettings(const EditorSettings&) = delete;
    EditorSettings& operator=(const EditorSettings&) = delete;
};

} // namespace Dot
