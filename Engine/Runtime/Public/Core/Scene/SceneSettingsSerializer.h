#pragma once

#include "Core/Core.h"
#include "Core/Scene/SceneSettingsAsset.h"

#include <filesystem>
#include <string>

namespace Dot
{

class DOT_CORE_API SceneSettingsSerializer
{
public:
    bool Save(const SceneSettingsAsset& asset, const std::filesystem::path& path);
    bool Load(SceneSettingsAsset& asset, const std::filesystem::path& path);

    const std::string& GetLastError() const { return m_LastError; }

private:
    std::string m_LastError;
};

} // namespace Dot
