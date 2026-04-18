// =============================================================================
// Dot Engine - Project Settings Storage
// =============================================================================

#pragma once

#include <filesystem>

namespace Dot
{

class ProjectSettingsStorage
{
public:
    static bool Load();
    static bool Save();

    static std::filesystem::path GetFilePath();
};

} // namespace Dot
