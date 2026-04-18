// =============================================================================
// Dot Engine - Shared Lua Autocomplete Entries
// =============================================================================

#pragma once

#include <string>
#include <vector>

namespace Dot
{

struct LuaAutoCompleteEntry
{
    std::string keyword;
    std::string signature;
    std::string description;
    std::string category;
    std::vector<std::string> requiredModules;
};

const std::vector<LuaAutoCompleteEntry>& GetLuaAutoCompleteEntries();
bool IsLuaAutoCompleteEntryVisible(const LuaAutoCompleteEntry& entry);

} // namespace Dot
