// =============================================================================
// Dot Engine - Lightmap Baker Settings
// =============================================================================

#pragma once

#include <cmath>

namespace Dot
{

enum class LightmapQualityPreset
{
    Preview = 0,
    Medium,
    High,
    Custom
};

enum class LightmapPreviewMode
{
    Combined = 0,
    BakedOnly,
    RealtimeOnly
};

class LightmapBakerSettings
{
public:
    struct QualityValues
    {
        float texelsPerUnit = 32.0f;
        int atlasSize = 1024;
        int padding = 4;
        int dilationMargin = 2;
    };

    static LightmapBakerSettings& Get()
    {
        static LightmapBakerSettings instance;
        return instance;
    }

    static constexpr QualityValues GetQualityValues(LightmapQualityPreset preset)
    {
        switch (preset)
        {
            case LightmapQualityPreset::Preview:
                return {8.0f, 512, 2, 1};
            case LightmapQualityPreset::High:
                return {64.0f, 2048, 8, 4};
            case LightmapQualityPreset::Custom:
            case LightmapQualityPreset::Medium:
            default:
                return {32.0f, 1024, 4, 2};
        }
    }

    static bool MatchesQualityValues(const QualityValues& left, const QualityValues& right)
    {
        return std::abs(left.texelsPerUnit - right.texelsPerUnit) <= 0.001f && left.atlasSize == right.atlasSize &&
               left.padding == right.padding && left.dilationMargin == right.dilationMargin;
    }

    void ApplyQualityPreset(LightmapQualityPreset preset)
    {
        if (preset == LightmapQualityPreset::Custom)
        {
            qualityPreset = LightmapQualityPreset::Custom;
            return;
        }

        const QualityValues values = GetQualityValues(preset);
        qualityPreset = preset;
        texelsPerUnit = values.texelsPerUnit;
        atlasSize = values.atlasSize;
        padding = values.padding;
        dilationMargin = values.dilationMargin;
    }

    void SyncQualityPresetFromValues()
    {
        const QualityValues current{texelsPerUnit, atlasSize, padding, dilationMargin};
        for (LightmapQualityPreset preset : {LightmapQualityPreset::Preview, LightmapQualityPreset::Medium,
                                             LightmapQualityPreset::High})
        {
            if (MatchesQualityValues(current, GetQualityValues(preset)))
            {
                qualityPreset = preset;
                return;
            }
        }

        qualityPreset = LightmapQualityPreset::Custom;
    }

    void ResetToDefaults()
    {
        previewMode = LightmapPreviewMode::Combined;
        ApplyQualityPreset(LightmapQualityPreset::Medium);
    }

    float texelsPerUnit = 32.0f;
    int atlasSize = 1024;
    int padding = 4;
    int dilationMargin = 2;
    LightmapQualityPreset qualityPreset = LightmapQualityPreset::Medium;
    LightmapPreviewMode previewMode = LightmapPreviewMode::Combined;

private:
    LightmapBakerSettings() { ResetToDefaults(); }
};

} // namespace Dot
