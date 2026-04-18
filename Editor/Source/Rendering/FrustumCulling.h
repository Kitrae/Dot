#pragma once

#include "Camera.h"

#include <algorithm>
#include <cmath>

namespace Dot
{

struct SpatialSplitFrustumSettings
{
    float paddingBase = 0.05f;
    float paddingScale = 0.02f;
    float minSplitExtent = 6.0f;
    float targetChunkSize = 8.0f;
    float thinAxisThreshold = 3.0f;
    int maxSplitX = 4;
    int maxSplitY = 3;
    int maxSplitZ = 4;
};

struct SpatialSplitFrustumInfo
{
    float paddedMinX = 0.0f;
    float paddedMinY = 0.0f;
    float paddedMinZ = 0.0f;
    float paddedMaxX = 0.0f;
    float paddedMaxY = 0.0f;
    float paddedMaxZ = 0.0f;
    int splitX = 1;
    int splitY = 1;
    int splitZ = 1;

    bool UsesSplits() const { return splitX > 1 || splitY > 1 || splitZ > 1; }
    int ChunkCount() const { return splitX * splitY * splitZ; }
};

struct SpatialSplitFrustumCounters
{
    int frustumTestedObjects = 0;
    int chunkedObjects = 0;
    int frustumCulledChunks = 0;
    int acceptedViaChunkObjects = 0;
};

inline int ComputeSpatialSplitCount(float extent, int maxSplits, const SpatialSplitFrustumSettings& settings)
{
    if (extent < settings.minSplitExtent || extent <= settings.thinAxisThreshold)
        return 1;

    return std::clamp(static_cast<int>(std::ceil(extent / settings.targetChunkSize)), 1, maxSplits);
}

inline SpatialSplitFrustumInfo BuildSpatialSplitFrustumInfo(float minX, float minY, float minZ, float maxX, float maxY,
                                                            float maxZ,
                                                            const SpatialSplitFrustumSettings& settings = {})
{
    const float extentX = std::max(0.0f, maxX - minX);
    const float extentY = std::max(0.0f, maxY - minY);
    const float extentZ = std::max(0.0f, maxZ - minZ);
    const float padding = settings.paddingBase + std::max({extentX, extentY, extentZ}) * settings.paddingScale;

    SpatialSplitFrustumInfo info;
    info.paddedMinX = minX - padding;
    info.paddedMinY = minY - padding;
    info.paddedMinZ = minZ - padding;
    info.paddedMaxX = maxX + padding;
    info.paddedMaxY = maxY + padding;
    info.paddedMaxZ = maxZ + padding;

    const float paddedExtentX = info.paddedMaxX - info.paddedMinX;
    const float paddedExtentY = info.paddedMaxY - info.paddedMinY;
    const float paddedExtentZ = info.paddedMaxZ - info.paddedMinZ;
    const float maxPaddedExtent = std::max({paddedExtentX, paddedExtentY, paddedExtentZ});

    if (maxPaddedExtent < settings.minSplitExtent)
        return info;

    info.splitX = ComputeSpatialSplitCount(paddedExtentX, settings.maxSplitX, settings);
    info.splitY = ComputeSpatialSplitCount(paddedExtentY, settings.maxSplitY, settings);
    info.splitZ = ComputeSpatialSplitCount(paddedExtentZ, settings.maxSplitZ, settings);

    return info;
}

template <typename F>
inline bool VisitVisibleSpatialSplitChunks(const Camera::Frustum& frustum, const SpatialSplitFrustumInfo& info, F&& visitor,
                                           SpatialSplitFrustumCounters* counters = nullptr)
{
    const float stepX = (info.paddedMaxX - info.paddedMinX) / static_cast<float>(info.splitX);
    const float stepY = (info.paddedMaxY - info.paddedMinY) / static_cast<float>(info.splitY);
    const float stepZ = (info.paddedMaxZ - info.paddedMinZ) / static_cast<float>(info.splitZ);

    bool accepted = false;
    for (int ix = 0; ix < info.splitX; ++ix)
    {
        const float subMinX = info.paddedMinX + stepX * static_cast<float>(ix);
        const float subMaxX = (ix == info.splitX - 1) ? info.paddedMaxX : (subMinX + stepX);
        for (int iy = 0; iy < info.splitY; ++iy)
        {
            const float subMinY = info.paddedMinY + stepY * static_cast<float>(iy);
            const float subMaxY = (iy == info.splitY - 1) ? info.paddedMaxY : (subMinY + stepY);
            for (int iz = 0; iz < info.splitZ; ++iz)
            {
                const float subMinZ = info.paddedMinZ + stepZ * static_cast<float>(iz);
                const float subMaxZ = (iz == info.splitZ - 1) ? info.paddedMaxZ : (subMinZ + stepZ);
                if (frustum.TestAABB(subMinX, subMinY, subMinZ, subMaxX, subMaxY, subMaxZ))
                {
                    accepted = true;
                    visitor(subMinX, subMinY, subMinZ, subMaxX, subMaxY, subMaxZ, ix, iy, iz);
                }
                else if (counters)
                {
                    counters->frustumCulledChunks++;
                }
            }
        }
    }

    if (accepted && counters)
        counters->acceptedViaChunkObjects++;
    return accepted;
}

inline bool TestAABBWithSpatialSplits(const Camera::Frustum& frustum, float minX, float minY, float minZ, float maxX,
                                      float maxY, float maxZ, SpatialSplitFrustumCounters* counters = nullptr,
                                      const SpatialSplitFrustumSettings& settings = {})
{
    if (counters)
        counters->frustumTestedObjects++;

    const SpatialSplitFrustumInfo info = BuildSpatialSplitFrustumInfo(minX, minY, minZ, maxX, maxY, maxZ, settings);
    const bool fullVisible =
        frustum.TestAABB(info.paddedMinX, info.paddedMinY, info.paddedMinZ, info.paddedMaxX, info.paddedMaxY,
                         info.paddedMaxZ);

    if (!info.UsesSplits())
        return fullVisible;

    if (counters)
        counters->chunkedObjects++;

    const bool splitVisible = VisitVisibleSpatialSplitChunks(frustum, info,
                                                             [](float, float, float, float, float, float, int, int, int)
                                                             {
                                                             },
                                                             counters);
    return splitVisible || fullVisible;
}

} // namespace Dot
