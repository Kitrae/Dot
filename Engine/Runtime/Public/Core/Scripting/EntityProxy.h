// =============================================================================
// Dot Engine - Entity Proxy for Lua Scripting
// =============================================================================
// Roblox-style proxy that wraps Entity references for clean Lua API.
// Uses weak references to allow Lua GC to clean up unused proxies.
// =============================================================================

#pragma once

#include "Core/Core.h"
#include "Core/ECS/Entity.h"

#include <cstdint>
#include <string>

namespace Dot
{

class World;

/// EntityProxy - Lightweight wrapper around an Entity for Lua scripting.
///
/// This proxy provides a clean object-oriented API:
///   self.Position = Vec3(1, 2, 3)
///   self.Rotation = self.Rotation + Vec3(0, 45 * dt, 0)
///
/// Instead of the cumbersome raw function approach:
///   SetPosition(_entityIndex, _entityGen, Vec3(1, 2, 3))
///
/// The proxy stores:
/// - Entity index + generation for identifying the C++ entity
/// - Pointer to World for component access
/// - Validity flag to detect destroyed entities
struct DOT_CORE_API EntityProxy
{
    uint32 entityIndex = 0;     ///< Entity index in the ECS
    uint8 entityGeneration = 0; ///< Generation to detect stale references
    World* world = nullptr;     ///< Pointer to the world (non-owning)

    /// Default constructor
    EntityProxy() = default;

    /// Construct from entity and world
    EntityProxy(Entity entity, World* w)
        : entityIndex(entity.GetIndex()), entityGeneration(entity.GetGeneration()), world(w)
    {
    }

    /// Get the underlying Entity
    Entity GetEntity() const { return Entity(entityIndex, entityGeneration); }

    /// Check if this proxy points to a valid, living entity
    bool IsValid() const;

    /// Get entity name
    std::string GetName() const;

    /// Set entity name
    void SetName(const std::string& name);

    /// Check if entity has a HealthComponent
    bool HasHealth() const;

    /// Get/set current health
    float GetHealth() const;
    void SetHealth(float health);

    /// Get/set max health
    float GetMaxHealth() const;
    void SetMaxHealth(float maxHealth);

    /// Get/set invulnerability
    bool GetInvulnerable() const;
    void SetInvulnerable(bool invulnerable);

    /// Get/set destroy-on-death behavior
    bool GetDestroyOnDeath() const;
    void SetDestroyOnDeath(bool destroyOnDeath);

    /// Health state helpers
    bool IsDead() const;
    float GetHealthPercent() const;
    float ApplyDamage(float amount);
    float Heal(float amount);
    void RestoreFullHealth();

    /// Render layer helpers
    uint32 GetRenderLayerMask() const;
    void SetRenderLayerMask(uint32 mask);
    bool GetInWorldLayer() const;
    void SetInWorldLayer(bool enabled);
    bool GetInViewmodelLayer() const;
    void SetInViewmodelLayer(bool enabled);
};

} // namespace Dot
