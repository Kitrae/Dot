#pragma once

#include "Core/Core.h"

#include <algorithm>

namespace Dot
{

struct DOT_CORE_API HealthComponent
{
    float currentHealth = 100.0f;
    float maxHealth = 100.0f;
    bool invulnerable = false;
    bool destroyEntityOnDeath = false;

    void Clamp()
    {
        maxHealth = std::max(0.0f, maxHealth);
        currentHealth = std::clamp(currentHealth, 0.0f, maxHealth);
    }

    bool IsDead() const { return currentHealth <= 0.0f; }

    float GetHealthFraction() const
    {
        if (maxHealth <= 0.0f)
            return 0.0f;
        return currentHealth / maxHealth;
    }

    void SetCurrentHealth(float value)
    {
        currentHealth = value;
        Clamp();
    }

    void SetMaxHealth(float value)
    {
        maxHealth = value;
        Clamp();
    }

    float ApplyDamage(float amount)
    {
        if (invulnerable || amount <= 0.0f)
            return currentHealth;

        currentHealth -= amount;
        Clamp();
        return currentHealth;
    }

    float Heal(float amount)
    {
        if (amount <= 0.0f)
            return currentHealth;

        currentHealth += amount;
        Clamp();
        return currentHealth;
    }

    void RestoreFullHealth() { currentHealth = maxHealth; }
};

} // namespace Dot
