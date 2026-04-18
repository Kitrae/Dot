#pragma once

#include <string>

namespace Dot
{

struct MapComponent
{
    std::string mapPath;
    bool visible = true;
    bool collisionEnabled = true;
};

} // namespace Dot
