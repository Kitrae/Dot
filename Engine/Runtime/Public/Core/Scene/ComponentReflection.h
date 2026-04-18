// =============================================================================
// Dot Engine - Component Reflection
// =============================================================================
// Forward declaration for component reflection registration.
// =============================================================================

#pragma once

namespace Dot
{

/// Register all scene components with the reflection system.
/// Must be called before using TypeRegistry for scene components.
void RegisterSceneComponents();

} // namespace Dot
