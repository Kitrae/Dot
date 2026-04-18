// =============================================================================
// Dot Engine - Entity Proxy Implementation
// =============================================================================

#include "Core/Scripting/EntityProxy.h"

#include "Core/ECS/World.h"
#include "Core/Gameplay/HealthComponent.h"
#include "Core/Scene/Components.h"

namespace Dot
{

bool EntityProxy::IsValid() const
{
    if (!world)
        return false;
    return world->IsAlive(GetEntity());
}

std::string EntityProxy::GetName() const
{
    if (!IsValid())
        return "";

    NameComponent* name = world->GetComponent<NameComponent>(GetEntity());
    return name ? name->name : "";
}

void EntityProxy::SetName(const std::string& name)
{
    if (!IsValid())
        return;

    NameComponent* nameComp = world->GetComponent<NameComponent>(GetEntity());
    if (nameComp)
        nameComp->name = name;
}

bool EntityProxy::HasHealth() const
{
    return IsValid() && world->HasComponent<HealthComponent>(GetEntity());
}

float EntityProxy::GetHealth() const
{
    if (!IsValid())
        return 0.0f;

    const HealthComponent* health = world->GetComponent<HealthComponent>(GetEntity());
    return health ? health->currentHealth : 0.0f;
}

void EntityProxy::SetHealth(float healthValue)
{
    if (!IsValid())
        return;

    HealthComponent* health = world->GetComponent<HealthComponent>(GetEntity());
    if (!health)
        return;

    health->SetCurrentHealth(healthValue);
    if (health->destroyEntityOnDeath && health->IsDead())
        world->DestroyEntity(GetEntity());
}

float EntityProxy::GetMaxHealth() const
{
    if (!IsValid())
        return 0.0f;

    const HealthComponent* health = world->GetComponent<HealthComponent>(GetEntity());
    return health ? health->maxHealth : 0.0f;
}

void EntityProxy::SetMaxHealth(float maxHealthValue)
{
    if (!IsValid())
        return;

    HealthComponent* health = world->GetComponent<HealthComponent>(GetEntity());
    if (health)
        health->SetMaxHealth(maxHealthValue);
}

bool EntityProxy::GetInvulnerable() const
{
    if (!IsValid())
        return false;

    const HealthComponent* health = world->GetComponent<HealthComponent>(GetEntity());
    return health ? health->invulnerable : false;
}

void EntityProxy::SetInvulnerable(bool invulnerable)
{
    if (!IsValid())
        return;

    HealthComponent* health = world->GetComponent<HealthComponent>(GetEntity());
    if (health)
        health->invulnerable = invulnerable;
}

bool EntityProxy::GetDestroyOnDeath() const
{
    if (!IsValid())
        return false;

    const HealthComponent* health = world->GetComponent<HealthComponent>(GetEntity());
    return health ? health->destroyEntityOnDeath : false;
}

void EntityProxy::SetDestroyOnDeath(bool destroyOnDeath)
{
    if (!IsValid())
        return;

    HealthComponent* health = world->GetComponent<HealthComponent>(GetEntity());
    if (health)
        health->destroyEntityOnDeath = destroyOnDeath;
}

bool EntityProxy::IsDead() const
{
    if (!IsValid())
        return false;

    const HealthComponent* health = world->GetComponent<HealthComponent>(GetEntity());
    return health ? health->IsDead() : false;
}

float EntityProxy::GetHealthPercent() const
{
    if (!IsValid())
        return 0.0f;

    const HealthComponent* health = world->GetComponent<HealthComponent>(GetEntity());
    return health ? health->GetHealthFraction() : 0.0f;
}

float EntityProxy::ApplyDamage(float amount)
{
    if (!IsValid())
        return 0.0f;

    HealthComponent* health = world->GetComponent<HealthComponent>(GetEntity());
    if (!health)
        return 0.0f;

    const float remainingHealth = health->ApplyDamage(amount);
    if (health->destroyEntityOnDeath && health->IsDead())
        world->DestroyEntity(GetEntity());
    return remainingHealth;
}

float EntityProxy::Heal(float amount)
{
    if (!IsValid())
        return 0.0f;

    HealthComponent* health = world->GetComponent<HealthComponent>(GetEntity());
    return health ? health->Heal(amount) : 0.0f;
}

void EntityProxy::RestoreFullHealth()
{
    if (!IsValid())
        return;

    HealthComponent* health = world->GetComponent<HealthComponent>(GetEntity());
    if (health)
        health->RestoreFullHealth();
}

uint32 EntityProxy::GetRenderLayerMask() const
{
    if (!IsValid())
        return RenderLayerMask::World;

    const RenderLayerComponent* renderLayer = world->GetComponent<RenderLayerComponent>(GetEntity());
    return renderLayer ? renderLayer->mask : RenderLayerMask::World;
}

void EntityProxy::SetRenderLayerMask(uint32 mask)
{
    if (!IsValid())
        return;

    RenderLayerComponent* renderLayer = world->GetComponent<RenderLayerComponent>(GetEntity());
    if (!renderLayer)
        renderLayer = &world->AddComponent<RenderLayerComponent>(GetEntity());
    renderLayer->mask = mask;
}

bool EntityProxy::GetInWorldLayer() const
{
    return (GetRenderLayerMask() & RenderLayerMask::World) != 0;
}

void EntityProxy::SetInWorldLayer(bool enabled)
{
    uint32 mask = GetRenderLayerMask();
    if (enabled)
        mask |= RenderLayerMask::World;
    else
        mask &= ~RenderLayerMask::World;
    SetRenderLayerMask(mask);
}

bool EntityProxy::GetInViewmodelLayer() const
{
    return (GetRenderLayerMask() & RenderLayerMask::Viewmodel) != 0;
}

void EntityProxy::SetInViewmodelLayer(bool enabled)
{
    uint32 mask = GetRenderLayerMask();
    if (enabled)
        mask |= RenderLayerMask::Viewmodel;
    else
        mask &= ~RenderLayerMask::Viewmodel;
    SetRenderLayerMask(mask);
}

} // namespace Dot
