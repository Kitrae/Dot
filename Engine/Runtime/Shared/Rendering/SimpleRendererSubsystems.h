// =============================================================================
// Dot Engine - Simple Renderer Subsystem State
// =============================================================================

#pragma once

#include <cstdint>
#include <vector>

namespace Dot
{

struct HZBMipReadbackInfo
{
    uint64_t offset = 0;
    uint32_t rowPitch = 0;
    uint32_t width = 0;
    uint32_t height = 0;
};

struct OcclusionState
{
    void* hzbTexture = nullptr;        // ID3D12Resource* - Depth pyramid with mip chain
    void* hzbSrvHeap = nullptr;        // Combined SRV/UAV heap for HZB mips
    void* hzbUavHeap = nullptr;        // Legacy/unused (kept for compatibility)
    void* hzbDownsamplePSO = nullptr;  // Compute PSO for mip generation
    void* hzbRootSignature = nullptr;  // Root signature for HZB compute
    void* hzbReadbackBuffer = nullptr; // ID3D12Resource* readback staging for CPU occlusion tests
    std::vector<HZBMipReadbackInfo> mipReadback;
    std::vector<uint8_t> readbackData;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t mipLevels = 0;
    bool readbackValid = false;
    bool enabled = true;
    uint32_t readbackIntervalFrames = 4;
};

} // namespace Dot
