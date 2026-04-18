// =============================================================================
// Dot Engine - Collision Layers
// =============================================================================

#include "Core/Physics/CollisionLayers.h"

namespace Dot
{

CollisionLayers& CollisionLayers::Get()
{
    static CollisionLayers s_Instance;
    return s_Instance;
}

CollisionLayers::CollisionLayers()
{
    ResetDefaults();
}

void CollisionLayers::ResetDefaults()
{
    for (uint8 i = 0; i < kMaxLayers; ++i)
    {
        m_LayerNames[i].clear();
        m_LayerMatrix[i] = kAllLayersMask;
    }

    m_LayerNames[0] = "Default";
    m_LayerNames[1] = "Player";
    m_LayerNames[2] = "World";
    m_LayerNames[3] = "Trigger";
    m_LayerNames[4] = "Enemy";
    m_LayerNames[5] = "Pickup";
    m_LayerNames[6] = "Projectile";
}

const std::string& CollisionLayers::GetLayerName(uint8 layer) const
{
    static const std::string s_Empty;
    return (layer < kMaxLayers) ? m_LayerNames[layer] : s_Empty;
}

std::string CollisionLayers::GetLayerDisplayName(uint8 layer) const
{
    if (layer >= kMaxLayers)
        return "Invalid";

    if (!m_LayerNames[layer].empty())
        return m_LayerNames[layer];

    return "Layer " + std::to_string(layer);
}

void CollisionLayers::SetLayerName(uint8 layer, const std::string& name)
{
    if (layer >= kMaxLayers)
        return;

    m_LayerNames[layer] = name;
}

bool CollisionLayers::IsLayerActive(uint8 layer) const
{
    if (layer >= kMaxLayers)
        return false;

    return !m_LayerNames[layer].empty();
}

uint32 CollisionLayers::GetActiveLayerMask() const
{
    uint32 mask = 0;
    for (uint8 i = 0; i < kMaxLayers; ++i)
    {
        if (IsLayerActive(i))
            mask |= LayerBit(i);
    }
    return mask;
}

bool CollisionLayers::ShouldLayersCollide(uint8 layerA, uint8 layerB) const
{
    if (layerA >= kMaxLayers || layerB >= kMaxLayers)
        return false;

    return (m_LayerMatrix[layerA] & LayerBit(layerB)) != 0;
}

void CollisionLayers::SetLayersCollide(uint8 layerA, uint8 layerB, bool shouldCollide)
{
    if (layerA >= kMaxLayers || layerB >= kMaxLayers)
        return;

    if (shouldCollide)
    {
        m_LayerMatrix[layerA] |= LayerBit(layerB);
        m_LayerMatrix[layerB] |= LayerBit(layerA);
    }
    else
    {
        m_LayerMatrix[layerA] &= ~LayerBit(layerB);
        m_LayerMatrix[layerB] &= ~LayerBit(layerA);
    }
}

bool CollisionLayers::ShouldCollide(uint8 layerA, uint32 maskA, uint8 layerB, uint32 maskB) const
{
    if (layerA >= kMaxLayers || layerB >= kMaxLayers)
        return false;

    const bool aWantsB = (maskA & LayerBit(layerB)) != 0;
    const bool bWantsA = (maskB & LayerBit(layerA)) != 0;
    return aWantsB && bWantsA && ShouldLayersCollide(layerA, layerB);
}

uint32 CollisionLayers::GetDefaultCollisionMask(uint8 layer) const
{
    return (layer < kMaxLayers) ? m_LayerMatrix[layer] : 0u;
}

} // namespace Dot
