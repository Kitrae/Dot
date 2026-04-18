// =============================================================================
// Dot Engine - Collision Layers
// =============================================================================

#pragma once

#include "Core/Core.h"

#include <array>
#include <string>

namespace Dot
{

class DOT_CORE_API CollisionLayers
{
public:
    static constexpr uint8 kMaxLayers = 32;
    static constexpr uint32 kAllLayersMask = 0xFFFFFFFFu;

    static CollisionLayers& Get();

    void ResetDefaults();

    const std::string& GetLayerName(uint8 layer) const;
    std::string GetLayerDisplayName(uint8 layer) const;
    void SetLayerName(uint8 layer, const std::string& name);

    bool IsLayerActive(uint8 layer) const;
    uint32 GetActiveLayerMask() const;

    bool ShouldLayersCollide(uint8 layerA, uint8 layerB) const;
    void SetLayersCollide(uint8 layerA, uint8 layerB, bool shouldCollide);

    bool ShouldCollide(uint8 layerA, uint32 maskA, uint8 layerB, uint32 maskB) const;
    uint32 GetDefaultCollisionMask(uint8 layer) const;

    static constexpr uint32 LayerBit(uint8 layer) { return (layer < kMaxLayers) ? (1u << layer) : 0u; }

private:
    CollisionLayers();

    std::array<std::string, kMaxLayers> m_LayerNames;
    std::array<uint32, kMaxLayers> m_LayerMatrix;
};

} // namespace Dot
