#pragma once

#include <string>
#include <unordered_map>

namespace Dot
{

class ToolboxSettings
{
public:
    static ToolboxSettings& Get()
    {
        static ToolboxSettings instance;
        return instance;
    }

    std::unordered_map<std::string, bool> moduleStates;
    bool showAdvancedModules = false;

private:
    ToolboxSettings() = default;
    ToolboxSettings(const ToolboxSettings&) = delete;
    ToolboxSettings& operator=(const ToolboxSettings&) = delete;
};

} // namespace Dot
