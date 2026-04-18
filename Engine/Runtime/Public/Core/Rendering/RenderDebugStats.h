#pragma once

namespace Dot
{

struct RenderDebugStats
{
    int lod0Draws = 0;
    int lod1Draws = 0;
    int lod2Draws = 0;

    void Reset()
    {
        lod0Draws = 0;
        lod1Draws = 0;
        lod2Draws = 0;
    }
};

inline RenderDebugStats g_RenderDebugStats;

} // namespace Dot
