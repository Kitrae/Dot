// =============================================================================
// Dot Engine - Scene Serializer
// =============================================================================

#pragma once

#include "Core/Core.h"

#include <string>

namespace Dot
{

class World;

class DOT_CORE_API SceneSerializer
{
public:
    SceneSerializer() = default;
    ~SceneSerializer() = default;

    bool Save(const World& world, const std::string& filepath);
    bool Load(World& world, const std::string& filepath);

    const std::string& GetLastError() const { return m_LastError; }
    void SetSceneSettingsReference(const std::string& sceneSettingsReference)
    {
        m_SceneSettingsReference = sceneSettingsReference;
    }
    const std::string& GetSceneSettingsReference() const { return m_SceneSettingsReference; }

private:
    std::string m_LastError;
    std::string m_SceneSettingsReference;
};

} // namespace Dot
