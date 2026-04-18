#pragma once

#include "Core/Map/MapTypes.h"

#include <string>

namespace Dot
{

class DOT_CORE_API MapSerializer
{
public:
    bool Save(const MapAsset& asset, const std::string& path);
    bool Load(MapAsset& asset, const std::string& path);

    const std::string& GetLastError() const { return m_LastError; }

private:
    std::string m_LastError;
};

} // namespace Dot
