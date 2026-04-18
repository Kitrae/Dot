// =============================================================================
// Dot Engine - UI Asset Serializer
// =============================================================================

#pragma once

#include "Core/Core.h"
#include "Core/UI/UIAsset.h"

#include <filesystem>
#include <string>

namespace Dot
{

class DOT_CORE_API UIAssetSerializer
{
public:
    bool Save(const UIAsset& asset, const std::filesystem::path& path);
    bool Load(UIAsset& asset, const std::filesystem::path& path);

    const std::string& GetLastError() const { return m_LastError; }

private:
    std::string m_LastError;
};

} // namespace Dot
