#pragma once

#include "Core/Core.h"

#include <filesystem>
#include <string>

namespace Dot
{

struct DotProjectAsset
{
    uint32 version = 1;
    std::string gameName = "Dot Game";
    std::string startupScene;
    uint32 windowWidth = 1600;
    uint32 windowHeight = 900;
    bool startFullscreen = false;
    bool captureMouseOnStart = true;
};

class DOT_CORE_API DotProjectSerializer
{
public:
    bool Save(const DotProjectAsset& asset, const std::filesystem::path& path);
    bool Load(DotProjectAsset& asset, const std::filesystem::path& path);

    const std::string& GetLastError() const { return m_LastError; }

private:
    std::string m_LastError;
};

} // namespace Dot
