#pragma once

#include "Core/Map/MapTypes.h"

namespace Dot
{

class DOT_CORE_API MapCompiler
{
public:
    static MapCompiledData Compile(const MapAsset& asset, uint64 revision = 0);
};

} // namespace Dot
