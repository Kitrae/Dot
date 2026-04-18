// =============================================================================
// Dot Engine - Entity
// =============================================================================
// Entity identifier with generation for safe handle reuse.
// =============================================================================

#pragma once

#include "Core/Core.h"

#include <functional>


namespace Dot
{

/// Entity identifier
/// Low 24 bits: index, High 8 bits: generation
/// Generation prevents use-after-delete bugs
struct Entity
{
    uint32 id = 0;

    // Constants
    static constexpr uint32 kIndexBits = 24;
    static constexpr uint32 kGenerationBits = 8;
    static constexpr uint32 kIndexMask = (1 << kIndexBits) - 1;
    static constexpr uint32 kGenerationMask = (1 << kGenerationBits) - 1;
    static constexpr uint32 kMaxEntities = kIndexMask;

    // Constructors
    constexpr Entity() = default;
    constexpr explicit Entity(uint32 id) : id(id) {}
    constexpr Entity(uint32 index, uint8 generation)
        : id((static_cast<uint32>(generation) << kIndexBits) | (index & kIndexMask))
    {
    }

    // Accessors
    constexpr uint32 GetIndex() const { return id & kIndexMask; }
    constexpr uint8 GetGeneration() const { return static_cast<uint8>((id >> kIndexBits) & kGenerationMask); }

    // Validity
    constexpr bool IsValid() const { return id != 0; }
    static constexpr Entity Invalid() { return Entity(0); }

    // Comparison
    constexpr bool operator==(Entity other) const { return id == other.id; }
    constexpr bool operator!=(Entity other) const { return id != other.id; }
    constexpr bool operator<(Entity other) const { return id < other.id; }

    // Hash support
    struct Hash
    {
        size_t operator()(Entity e) const { return std::hash<uint32>{}(e.id); }
    };
};

/// Null entity constant
constexpr Entity kNullEntity = Entity::Invalid();

} // namespace Dot
