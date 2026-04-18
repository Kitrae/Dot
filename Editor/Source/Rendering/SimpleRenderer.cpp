// =============================================================================
// Dot Engine - Simple Renderer Implementation
// =============================================================================

#include "SimpleRenderer.h"

#include "CubemapLoader.h"
#include "FbxLoader.h"
#include "ObjLoader.h"
#include "PrimitiveMeshes.h"
#include "SimpleRendererGraphPasses.h"
#include "ShaderCompiler.h"

#include <Core/Log.h>
#include <Core/Material/MaterialGraph.h>
#include <Core/Material/MaterialNode.h>
#include <Core/Material/NodeProperty.h>
#include <Core/Material/MaterialTextureUtils.h>
#include <Core/Rendering/RenderDebugStats.h>
#include <Core/Rendering/ViewSettings.h>
#include <Core/Scene/Components.h>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <d3d12.h>
#include <d3dcompiler.h>
#include <filesystem>
#include <functional>
#include <random>
#include <unordered_map>
#include <unordered_set>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

// Include D3D12 implementations for direct access
#include "D3D12/D3D12Buffer.h"
#include "D3D12/D3D12Device.h"
#include "D3D12/D3D12SwapChain.h"
#include "D3D12/D3D12Texture.h"

#include "Core/Assets/AssetManager.h"

// Helper to get D3D12 device/cmdlist (implementation depends on RHI internals)
extern ID3D12Device* GetD3D12DevicePtr(Dot::RHIDevice* device);
extern ID3D12GraphicsCommandList* GetD3D12CommandList(Dot::RHIDevice* device);
extern ID3D12Resource* GetD3D12Buffer(Dot::RHIBuffer* buffer);
extern D3D12_GPU_VIRTUAL_ADDRESS GetD3D12BufferGPUAddress(Dot::RHIBuffer* buffer);

namespace Dot
{

namespace
{

std::string ResolveShaderRootPath(const std::string& overridePath, bool allowEditorFallback)
{
    if (!overridePath.empty())
    {
        const std::filesystem::path overrideCandidate = std::filesystem::path(overridePath).lexically_normal();
        if (std::filesystem::exists(overrideCandidate))
            return overrideCandidate.string();
        return {};
    }

    std::filesystem::path candidate;

#ifdef _WIN32
    char modulePath[MAX_PATH] = {};
    const DWORD pathLength = GetModuleFileNameA(nullptr, modulePath, MAX_PATH);
    if (pathLength > 0)
    {
        const std::filesystem::path exeDir = std::filesystem::path(modulePath).parent_path();
        candidate = exeDir / "Shaders";
        if (std::filesystem::exists(candidate))
            return candidate.string();

        if (allowEditorFallback)
        {
            candidate = exeDir / "../../../Editor/Shaders";
            if (std::filesystem::exists(candidate))
                return candidate.lexically_normal().string();
        }
    }
#endif

    candidate = std::filesystem::current_path() / "Shaders";
    if (std::filesystem::exists(candidate))
        return candidate.string();

    if (allowEditorFallback)
    {
        candidate = std::filesystem::current_path() / "../../../Editor/Shaders";
        if (std::filesystem::exists(candidate))
            return candidate.lexically_normal().string();

        return "../../../Editor/Shaders";
    }

    return {};
}

uint32_t ComputeTextureMipCount(uint32_t width, uint32_t height)
{
    uint32_t mipLevels = 1;
    uint32_t maxDimension = (std::max)(width, height);
    while (maxDimension > 1)
    {
        maxDimension = (std::max)(maxDimension / 2u, 1u);
        ++mipLevels;
    }
    return mipLevels;
}

uint64_t ComputeCubemapSourceStamp(const std::string& path)
{
    namespace fs = std::filesystem;

    try
    {
        const fs::path sourcePath(path);
        if (!fs::exists(sourcePath))
            return 0;

        const auto encodeTime = [](const fs::file_time_type& timestamp) -> uint64_t
        {
            return static_cast<uint64_t>(timestamp.time_since_epoch().count());
        };

        std::error_code ec;
        uint64_t newestStamp = encodeTime(fs::last_write_time(sourcePath, ec));
        if (ec)
            newestStamp = 0;

        if (fs::is_directory(sourcePath, ec) && !ec)
        {
            for (const auto& entry : fs::directory_iterator(sourcePath, ec))
            {
                if (ec)
                    break;

                if (!entry.is_regular_file(ec) || ec)
                    continue;

                const uint64_t entryStamp = encodeTime(fs::last_write_time(entry.path(), ec));
                if (!ec)
                    newestStamp = (std::max)(newestStamp, entryStamp);
            }
        }

        return newestStamp;
    }
    catch (...)
    {
        return 0;
    }
}

std::vector<std::vector<uint8_t>> BuildRgbaMipChain(const std::vector<uint8_t>& baseLevel, uint32_t width,
                                                    uint32_t height)
{
    std::vector<std::vector<uint8_t>> mipChain;
    if (baseLevel.empty() || width == 0 || height == 0)
        return mipChain;

    mipChain.push_back(baseLevel);

    uint32_t currentWidth = width;
    uint32_t currentHeight = height;
    while (currentWidth > 1 || currentHeight > 1)
    {
        const uint32_t nextWidth = (std::max)(currentWidth / 2u, 1u);
        const uint32_t nextHeight = (std::max)(currentHeight / 2u, 1u);
        const std::vector<uint8_t>& src = mipChain.back();

        std::vector<uint8_t> dst(static_cast<size_t>(nextWidth) * nextHeight * 4u, 0);
        for (uint32_t y = 0; y < nextHeight; ++y)
        {
            for (uint32_t x = 0; x < nextWidth; ++x)
            {
                for (uint32_t channel = 0; channel < 4; ++channel)
                {
                    uint32_t sum = 0;
                    for (uint32_t sampleY = 0; sampleY < 2; ++sampleY)
                    {
                        for (uint32_t sampleX = 0; sampleX < 2; ++sampleX)
                        {
                            const uint32_t srcX = (std::min)(x * 2u + sampleX, currentWidth - 1u);
                            const uint32_t srcY = (std::min)(y * 2u + sampleY, currentHeight - 1u);
                            const size_t srcIndex =
                                (static_cast<size_t>(srcY) * currentWidth + srcX) * 4u + channel;
                            sum += src[srcIndex];
                        }
                    }

                    const size_t dstIndex = (static_cast<size_t>(y) * nextWidth + x) * 4u + channel;
                    dst[dstIndex] = static_cast<uint8_t>((sum + 2u) / 4u);
                }
            }
        }

        mipChain.push_back(std::move(dst));
        currentWidth = nextWidth;
        currentHeight = nextHeight;
    }

    return mipChain;
}

std::vector<std::vector<uint8_t>> BuildFloatRgbaMipChain(const std::vector<uint8_t>& baseLevel, uint32_t width,
                                                         uint32_t height)
{
    std::vector<std::vector<uint8_t>> mipChain;
    if (baseLevel.empty() || width == 0 || height == 0)
        return mipChain;

    mipChain.push_back(baseLevel);

    uint32_t currentWidth = width;
    uint32_t currentHeight = height;
    while (currentWidth > 1 || currentHeight > 1)
    {
        const uint32_t nextWidth = (std::max)(currentWidth / 2u, 1u);
        const uint32_t nextHeight = (std::max)(currentHeight / 2u, 1u);
        const std::vector<uint8_t>& src = mipChain.back();

        std::vector<uint8_t> dst(static_cast<size_t>(nextWidth) * nextHeight * 4u * sizeof(float), 0);
        const float* srcFloats = reinterpret_cast<const float*>(src.data());
        float* dstFloats = reinterpret_cast<float*>(dst.data());

        for (uint32_t y = 0; y < nextHeight; ++y)
        {
            for (uint32_t x = 0; x < nextWidth; ++x)
            {
                for (uint32_t channel = 0; channel < 4; ++channel)
                {
                    float sum = 0.0f;
                    for (uint32_t sampleY = 0; sampleY < 2; ++sampleY)
                    {
                        for (uint32_t sampleX = 0; sampleX < 2; ++sampleX)
                        {
                            const uint32_t srcX = (std::min)(x * 2u + sampleX, currentWidth - 1u);
                            const uint32_t srcY = (std::min)(y * 2u + sampleY, currentHeight - 1u);
                            const size_t srcIndex =
                                (static_cast<size_t>(srcY) * currentWidth + srcX) * 4u + channel;
                            sum += srcFloats[srcIndex];
                        }
                    }

                    const size_t dstIndex = (static_cast<size_t>(y) * nextWidth + x) * 4u + channel;
                    dstFloats[dstIndex] = sum * 0.25f;
                }
            }
        }

        mipChain.push_back(std::move(dst));
        currentWidth = nextWidth;
        currentHeight = nextHeight;
    }

    return mipChain;
}

bool UploadCubemapTexture(ID3D12Device* d3dDevice, ID3D12GraphicsCommandList* cmdList, ID3D12Resource* textureRes,
                          const CubemapData& cubemapData, uint32_t mipLevels,
                          std::vector<ComPtr<ID3D12Resource>>& uploadBuffers)
{
    if (!d3dDevice || !cmdList || !textureRes || !cubemapData.IsValid())
        return false;

    uploadBuffers.reserve(uploadBuffers.size() + CubemapData::FACE_COUNT * mipLevels);
    const uint32_t bytesPerPixel = static_cast<uint32_t>(cubemapData.channels) * cubemapData.bytesPerChannel;

    for (int face = 0; face < CubemapData::FACE_COUNT; ++face)
    {
        const std::vector<std::vector<uint8_t>> mipChain =
            cubemapData.IsHdr()
                ? BuildFloatRgbaMipChain(cubemapData.faceData[face], static_cast<uint32_t>(cubemapData.width),
                                         static_cast<uint32_t>(cubemapData.height))
                : BuildRgbaMipChain(cubemapData.faceData[face], static_cast<uint32_t>(cubemapData.width),
                                    static_cast<uint32_t>(cubemapData.height));

        uint32_t mipWidth = static_cast<uint32_t>(cubemapData.width);
        uint32_t mipHeight = static_cast<uint32_t>(cubemapData.height);
        for (uint32_t mip = 0; mip < mipLevels && mip < mipChain.size(); ++mip)
        {
            const UINT subresource = mip + (static_cast<UINT>(face) * mipLevels);
            D3D12_RESOURCE_DESC resourceDesc = textureRes->GetDesc();
            D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint = {};
            UINT numRows = 0;
            UINT64 rowSizeBytes = 0;
            UINT64 totalBytes = 0;
            d3dDevice->GetCopyableFootprints(&resourceDesc, subresource, 1, 0, &footprint, &numRows, &rowSizeBytes,
                                             &totalBytes);

            D3D12_HEAP_PROPERTIES uploadHeapProps = {};
            uploadHeapProps.Type = D3D12_HEAP_TYPE_UPLOAD;

            D3D12_RESOURCE_DESC uploadBufferDesc = {};
            uploadBufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
            uploadBufferDesc.Width = totalBytes;
            uploadBufferDesc.Height = 1;
            uploadBufferDesc.DepthOrArraySize = 1;
            uploadBufferDesc.MipLevels = 1;
            uploadBufferDesc.Format = DXGI_FORMAT_UNKNOWN;
            uploadBufferDesc.SampleDesc.Count = 1;
            uploadBufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

            ComPtr<ID3D12Resource> uploadBuffer;
            if (FAILED(d3dDevice->CreateCommittedResource(&uploadHeapProps, D3D12_HEAP_FLAG_NONE, &uploadBufferDesc,
                                                          D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                                          IID_PPV_ARGS(&uploadBuffer))))
            {
                return false;
            }

            void* mappedData = nullptr;
            D3D12_RANGE readRange = {0, 0};
            if (FAILED(uploadBuffer->Map(0, &readRange, &mappedData)))
                return false;

            const uint8_t* srcData = mipChain[mip].data();
            uint8_t* dstData = static_cast<uint8_t*>(mappedData);
            const UINT srcRowPitch = mipWidth * bytesPerPixel;
            const UINT dstRowPitch = footprint.Footprint.RowPitch;
            for (UINT row = 0; row < numRows; ++row)
                memcpy(dstData + row * dstRowPitch, srcData + row * srcRowPitch, srcRowPitch);
            uploadBuffer->Unmap(0, nullptr);

            D3D12_RESOURCE_BARRIER barrier = {};
            barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barrier.Transition.pResource = textureRes;
            barrier.Transition.Subresource = subresource;
            barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
            barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
            cmdList->ResourceBarrier(1, &barrier);

            D3D12_TEXTURE_COPY_LOCATION srcLocation = {};
            srcLocation.pResource = uploadBuffer.Get();
            srcLocation.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
            srcLocation.PlacedFootprint = footprint;

            D3D12_TEXTURE_COPY_LOCATION dstLocation = {};
            dstLocation.pResource = textureRes;
            dstLocation.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            dstLocation.SubresourceIndex = subresource;
            cmdList->CopyTextureRegion(&dstLocation, 0, 0, 0, &srcLocation, nullptr);

            barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
            barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
            cmdList->ResourceBarrier(1, &barrier);

            uploadBuffers.push_back(uploadBuffer);
            mipWidth = (std::max)(mipWidth / 2u, 1u);
            mipHeight = (std::max)(mipHeight / 2u, 1u);
        }
    }

    return true;
}

Vec3 ResolveReflectionProbeBoxExtents(const Vec3& boxExtents, float radius)
{
    if (boxExtents.x > 0.001f && boxExtents.y > 0.001f && boxExtents.z > 0.001f)
        return boxExtents;

    const float safeRadius = std::max(radius, 0.001f);
    return Vec3(safeRadius, safeRadius, safeRadius);
}

TextureSemantic ResolveTextureSemanticForMaterialSlot(const std::string& path, int sampleType)
{
    if (sampleType == static_cast<int>(TextureSampleType::Normal))
        return TextureSemantic::Normal;
    if (sampleType == static_cast<int>(TextureSampleType::Mask))
        return TextureSemantic::Mask;
    if (sampleType == static_cast<int>(TextureSampleType::Color))
        return TextureSemantic::Color;
    return GuessTextureSemanticFromPath(path);
}

DXGI_FORMAT GetTextureSrvFormat(RHIFormat format)
{
    switch (format)
    {
        case RHIFormat::R8G8B8A8_SRGB:
            return DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
        case RHIFormat::R8G8B8A8_UNORM:
            return DXGI_FORMAT_R8G8B8A8_UNORM;
        case RHIFormat::R32G32B32A32_FLOAT:
            return DXGI_FORMAT_R32G32B32A32_FLOAT;
        default:
            return DXGI_FORMAT_R8G8B8A8_UNORM;
    }
}

} // namespace

/// Helper: Extract texture path from a .dotmat file by loading MaterialGraph
static std::string GetSubmeshTexturePath(const std::string& dotmatPath)
{
    if (dotmatPath.empty())
        return "";

    std::string fullPath = AssetManager::Get().GetFullPath(dotmatPath);

    MaterialGraph graph;
    if (!graph.LoadFromFile(fullPath))
        return "";

    // Find Texture2D nodes and get their texture paths
    for (const auto& nodePtr : graph.GetNodes())
    {
        if (nodePtr && nodePtr->GetType() == MaterialNodeType::Texture2D)
        {
            auto* texProp = nodePtr->GetProperty<TexturePathProperty>();
            if (texProp && !texProp->path.empty())
            {
                return texProp->path;
            }
        }
    }

    return "";
}

struct LodCellKey
{
    int x = 0;
    int y = 0;
    int z = 0;

    bool operator==(const LodCellKey& other) const noexcept
    {
        return x == other.x && y == other.y && z == other.z;
    }
};

struct LodCellKeyHash
{
    size_t operator()(const LodCellKey& key) const noexcept
    {
        size_t hash = 1469598103934665603ull;
        hash ^= static_cast<size_t>(key.x);
        hash *= 1099511628211ull;
        hash ^= static_cast<size_t>(key.y);
        hash *= 1099511628211ull;
        hash ^= static_cast<size_t>(key.z);
        hash *= 1099511628211ull;
        return hash;
    }
};

struct LodTriangleKey
{
    uint32_t a = 0;
    uint32_t b = 0;
    uint32_t c = 0;

    bool operator==(const LodTriangleKey& other) const noexcept
    {
        return a == other.a && b == other.b && c == other.c;
    }
};

struct LodTriangleKeyHash
{
    size_t operator()(const LodTriangleKey& key) const noexcept
    {
        size_t hash = 1469598103934665603ull;
        hash ^= static_cast<size_t>(key.a);
        hash *= 1099511628211ull;
        hash ^= static_cast<size_t>(key.b);
        hash *= 1099511628211ull;
        hash ^= static_cast<size_t>(key.c);
        hash *= 1099511628211ull;
        return hash;
    }
};

static constexpr uint32_t kMinIndicesForGeneratedLod = 36;

static void RecomputeMeshBounds(MeshData& meshData)
{
    if (meshData.vertices.empty())
        return;

    meshData.boundsMinX = meshData.boundsMinY = meshData.boundsMinZ = FLT_MAX;
    meshData.boundsMaxX = meshData.boundsMaxY = meshData.boundsMaxZ = -FLT_MAX;

    for (const PrimitiveVertex& vertex : meshData.vertices)
    {
        meshData.boundsMinX = std::min(meshData.boundsMinX, vertex.x);
        meshData.boundsMinY = std::min(meshData.boundsMinY, vertex.y);
        meshData.boundsMinZ = std::min(meshData.boundsMinZ, vertex.z);
        meshData.boundsMaxX = std::max(meshData.boundsMaxX, vertex.x);
        meshData.boundsMaxY = std::max(meshData.boundsMaxY, vertex.y);
        meshData.boundsMaxZ = std::max(meshData.boundsMaxZ, vertex.z);
    }
}

static void GetMeshLodScreenHeightThresholds(const PrimitiveMesh& mesh, float aggressiveness, float& lod1Threshold,
                                             float& lod2Threshold)
{
    const float scale = std::max(aggressiveness, 0.01f);
    const float baseLod1 = std::max(mesh.lodScreenHeightThresholds[1], 0.0f);
    const float baseLod2 = std::clamp(mesh.lodScreenHeightThresholds[2], 0.0f, baseLod1);
    lod1Threshold = baseLod1 * scale;
    lod2Threshold = baseLod2 * scale;
}

static void ConfigurePrimitiveLodPolicy(PrimitiveType primitiveType, PrimitiveMesh& mesh)
{
    float lod1Threshold = 0.22f;
    float lod2Threshold = 0.08f;
    GetPrimitiveDefaultLodScreenHeightThresholds(primitiveType, lod1Threshold, lod2Threshold);
    mesh.lodScreenHeightThresholds = {0.0f, lod1Threshold, lod2Threshold};
}

static bool ApplyAuthoredPrimitiveLod(SimpleRenderer& renderer, PrimitiveMesh& targetMesh, uint32_t lodLevel,
                                      const MeshData& lodMeshData)
{
    if (lodLevel == 0 || lodLevel >= PrimitiveMesh::kLodCount || lodMeshData.vertices.empty() || lodMeshData.indices.empty())
        return false;

    const uint32_t previousVertexCount =
        (targetMesh.lodVertexCounts[lodLevel - 1] > 0) ? targetMesh.lodVertexCounts[lodLevel - 1] : targetMesh.lodVertexCounts[0];
    const uint32_t previousIndexCount =
        (targetMesh.lodIndexCounts[lodLevel - 1] > 0) ? targetMesh.lodIndexCounts[lodLevel - 1] : targetMesh.lodIndexCounts[0];

    std::unique_ptr<PrimitiveMesh> authoredMesh = renderer.CreateRuntimeMesh(lodMeshData);
    if (!authoredMesh || !authoredMesh->vertexBuffer || !authoredMesh->indexBuffer)
        return false;

    const bool isCheaper =
        authoredMesh->lodVertexCounts[0] < previousVertexCount || authoredMesh->lodIndexCounts[0] < previousIndexCount;
    if (!isCheaper)
        return false;

    targetMesh.lodVertexBuffers[lodLevel] = authoredMesh->vertexBuffer;
    targetMesh.lodVertexCounts[lodLevel] = authoredMesh->lodVertexCounts[0];
    targetMesh.lodIndexBuffers[lodLevel] = authoredMesh->indexBuffer;
    targetMesh.lodIndexCounts[lodLevel] = authoredMesh->lodIndexCounts[0];
    targetMesh.lodSubmeshes[lodLevel] = std::move(authoredMesh->submeshes);
    targetMesh.lodRenderBatches[lodLevel] = std::move(authoredMesh->lodRenderBatches[0]);
    targetMesh.lodHasTexturedSubmeshes[lodLevel] = authoredMesh->lodHasTexturedSubmeshes[0];
    return true;
}

static std::vector<uint32_t> BuildClusterLodIndices(const MeshData& meshData, uint32_t indexStart, uint32_t indexCount,
                                                    float targetRatio)
{
    if (meshData.vertices.size() < 24 || indexCount < kMinIndicesForGeneratedLod || targetRatio >= 1.0f)
        return {};
    if (indexStart >= meshData.indices.size() || indexCount > meshData.indices.size() - indexStart)
        return {};

    const float minX = meshData.boundsMinX;
    const float minY = meshData.boundsMinY;
    const float minZ = meshData.boundsMinZ;
    const float extentX = std::max(meshData.boundsMaxX - meshData.boundsMinX, 0.0001f);
    const float extentY = std::max(meshData.boundsMaxY - meshData.boundsMinY, 0.0001f);
    const float extentZ = std::max(meshData.boundsMaxZ - meshData.boundsMinZ, 0.0001f);
    const float longestExtent = std::max({extentX, extentY, extentZ});
    if (longestExtent <= 0.0001f)
        return {};

    const float targetVertexCount = std::max(16.0f, static_cast<float>(meshData.vertices.size()) * targetRatio);
    const float cellsPerAxisHint = std::max(2.0f, std::cbrt(targetVertexCount));
    const int splitX = std::clamp(static_cast<int>(std::round(cellsPerAxisHint * (extentX / longestExtent))), 1, 32);
    const int splitY = std::clamp(static_cast<int>(std::round(cellsPerAxisHint * (extentY / longestExtent))), 1, 32);
    const int splitZ = std::clamp(static_cast<int>(std::round(cellsPerAxisHint * (extentZ / longestExtent))), 1, 32);
    const float cellSizeX = extentX / static_cast<float>(splitX);
    const float cellSizeY = extentY / static_cast<float>(splitY);
    const float cellSizeZ = extentZ / static_cast<float>(splitZ);

    struct CellRepresentative
    {
        uint32_t vertexIndex = 0;
        float distanceSq = FLT_MAX;
    };

    std::unordered_map<LodCellKey, CellRepresentative, LodCellKeyHash> representatives;
    representatives.reserve(meshData.vertices.size());
    std::vector<LodCellKey> vertexCells(meshData.vertices.size());

    for (size_t vertexIndex = 0; vertexIndex < meshData.vertices.size(); ++vertexIndex)
    {
        const PrimitiveVertex& vertex = meshData.vertices[vertexIndex];
        const int cellX = std::min(splitX - 1, std::max(0, static_cast<int>((vertex.x - minX) / cellSizeX)));
        const int cellY = std::min(splitY - 1, std::max(0, static_cast<int>((vertex.y - minY) / cellSizeY)));
        const int cellZ = std::min(splitZ - 1, std::max(0, static_cast<int>((vertex.z - minZ) / cellSizeZ)));

        LodCellKey key{cellX, cellY, cellZ};
        vertexCells[vertexIndex] = key;

        const float centerX = minX + (static_cast<float>(cellX) + 0.5f) * cellSizeX;
        const float centerY = minY + (static_cast<float>(cellY) + 0.5f) * cellSizeY;
        const float centerZ = minZ + (static_cast<float>(cellZ) + 0.5f) * cellSizeZ;
        const float dx = vertex.x - centerX;
        const float dy = vertex.y - centerY;
        const float dz = vertex.z - centerZ;
        const float distanceSq = dx * dx + dy * dy + dz * dz;

        auto& representative = representatives[key];
        if (distanceSq < representative.distanceSq)
        {
            representative.vertexIndex = static_cast<uint32_t>(vertexIndex);
            representative.distanceSq = distanceSq;
        }
    }

    std::vector<uint32_t> remap(meshData.vertices.size(), 0);
    for (size_t vertexIndex = 0; vertexIndex < meshData.vertices.size(); ++vertexIndex)
    {
        auto it = representatives.find(vertexCells[vertexIndex]);
        if (it == representatives.end())
            return {};
        remap[vertexIndex] = it->second.vertexIndex;
    }

    std::unordered_set<LodTriangleKey, LodTriangleKeyHash> seenTriangles;
    seenTriangles.reserve(indexCount / 3);
    std::vector<uint32_t> simplifiedIndices;
    simplifiedIndices.reserve(static_cast<size_t>(static_cast<float>(indexCount) * std::max(targetRatio, 0.2f)));

    for (size_t index = indexStart; index + 2 < static_cast<size_t>(indexStart + indexCount); index += 3)
    {
        const uint32_t srcA = meshData.indices[index + 0];
        const uint32_t srcB = meshData.indices[index + 1];
        const uint32_t srcC = meshData.indices[index + 2];
        if (srcA >= remap.size() || srcB >= remap.size() || srcC >= remap.size())
            return {};

        const uint32_t a = remap[srcA];
        const uint32_t b = remap[srcB];
        const uint32_t c = remap[srcC];
        if (a == b || b == c || a == c)
            continue;

        LodTriangleKey dedupeKey{
            std::min({a, b, c}),
            a + b + c - std::min({a, b, c}) - std::max({a, b, c}),
            std::max({a, b, c})
        };
        if (!seenTriangles.insert(dedupeKey).second)
            continue;

        simplifiedIndices.push_back(a);
        simplifiedIndices.push_back(b);
        simplifiedIndices.push_back(c);
    }

    if (simplifiedIndices.size() < 12 || simplifiedIndices.size() >= (indexCount * 98) / 100)
        return {};

    return simplifiedIndices;
}

static bool BuildClusterDecimatedMesh(const MeshData& sourceMesh, float targetRatio, MeshData& outMesh)
{
    if (sourceMesh.vertices.size() < 24 || sourceMesh.indices.size() < kMinIndicesForGeneratedLod || targetRatio >= 1.0f)
        return false;
    constexpr int kNormalBuckets = 4;
    constexpr int kUvBuckets = 8;
    auto quantizeSignedUnit = [](float value) -> int {
        const float normalized = std::clamp((value + 1.0f) * 0.5f, 0.0f, 0.9999f);
        return std::clamp(static_cast<int>(normalized * kNormalBuckets), 0, kNormalBuckets - 1);
    };
    auto quantizeRange = [](float value, float minValue, float maxValue, int bucketCount) -> int {
        const float extent = std::max(maxValue - minValue, 0.0001f);
        const float normalized = std::clamp((value - minValue) / extent, 0.0f, 0.9999f);
        return std::clamp(static_cast<int>(normalized * static_cast<float>(bucketCount)), 0, bucketCount - 1);
    };

    struct ClusterKey
    {
        int x = 0;
        int y = 0;
        int z = 0;
        int nx = 0;
        int ny = 0;
        int nz = 0;
        int u = 0;
        int v = 0;
        int u2 = 0;
        int v2 = 0;

        bool operator==(const ClusterKey& other) const noexcept
        {
            return x == other.x && y == other.y && z == other.z && nx == other.nx && ny == other.ny &&
                   nz == other.nz && u == other.u && v == other.v && u2 == other.u2 && v2 == other.v2;
        }
    };
    struct ClusterKeyHash
    {
        size_t operator()(const ClusterKey& key) const noexcept
        {
            size_t hash = 1469598103934665603ull;
            hash ^= static_cast<size_t>(key.x);
            hash *= 1099511628211ull;
            hash ^= static_cast<size_t>(key.y);
            hash *= 1099511628211ull;
            hash ^= static_cast<size_t>(key.z);
            hash *= 1099511628211ull;
            hash ^= static_cast<size_t>(key.nx);
            hash *= 1099511628211ull;
            hash ^= static_cast<size_t>(key.ny);
            hash *= 1099511628211ull;
            hash ^= static_cast<size_t>(key.nz);
            hash *= 1099511628211ull;
            hash ^= static_cast<size_t>(key.u);
            hash *= 1099511628211ull;
            hash ^= static_cast<size_t>(key.v);
            hash *= 1099511628211ull;
            hash ^= static_cast<size_t>(key.u2);
            hash *= 1099511628211ull;
            hash ^= static_cast<size_t>(key.v2);
            hash *= 1099511628211ull;
            return hash;
        }
    };
    struct ClusterRepresentative
    {
        uint32_t newVertexIndex = 0;
        float distanceSq = FLT_MAX;
    };
    outMesh = {};
    outMesh.vertices.reserve(static_cast<size_t>(static_cast<float>(sourceMesh.vertices.size()) * targetRatio * 1.5f));

    auto appendOriginalSubmesh = [&](uint32_t sourceIndexStart, uint32_t sourceIndexCount,
                                     const Submesh* sourceSubmesh) -> bool {
        if (sourceIndexCount == 0 || sourceIndexStart >= sourceMesh.indices.size() ||
            sourceIndexCount > sourceMesh.indices.size() - sourceIndexStart)
        {
            return true;
        }

        Submesh outSubmesh = {};
        if (sourceSubmesh)
        {
            outSubmesh.materialPath = sourceSubmesh->materialPath;
            outSubmesh.texturePath = sourceSubmesh->texturePath;
            outSubmesh.indexStart = static_cast<uint32_t>(outMesh.indices.size());
        }

        std::unordered_map<uint32_t, uint32_t> sourceToOutVertex;
        sourceToOutVertex.reserve(sourceIndexCount);

        for (size_t index = sourceIndexStart; index + 2 < static_cast<size_t>(sourceIndexStart + sourceIndexCount); index += 3)
        {
            for (int corner = 0; corner < 3; ++corner)
            {
                const uint32_t srcIndex = sourceMesh.indices[index + corner];
                if (srcIndex >= sourceMesh.vertices.size())
                    return false;

                auto [it, inserted] =
                    sourceToOutVertex.emplace(srcIndex, static_cast<uint32_t>(outMesh.vertices.size()));
                if (inserted)
                    outMesh.vertices.push_back(sourceMesh.vertices[srcIndex]);
                outMesh.indices.push_back(it->second);
            }

            if (sourceSubmesh)
                outSubmesh.indexCount += 3;
        }

        if (sourceSubmesh && outSubmesh.indexCount > 0)
            outMesh.submeshes.push_back(std::move(outSubmesh));
        return true;
    };

    auto appendDecimatedSubmesh = [&](uint32_t sourceIndexStart, uint32_t sourceIndexCount,
                                      const Submesh* sourceSubmesh) -> bool {
        if (sourceIndexCount == 0 || sourceIndexStart >= sourceMesh.indices.size() ||
            sourceIndexCount > sourceMesh.indices.size() - sourceIndexStart)
        {
            return true;
        }

        std::unordered_set<uint32_t> usedVertices;
        usedVertices.reserve(sourceIndexCount);

        float localMinX = FLT_MAX, localMinY = FLT_MAX, localMinZ = FLT_MAX;
        float localMaxX = -FLT_MAX, localMaxY = -FLT_MAX, localMaxZ = -FLT_MAX;
        float localMinU = FLT_MAX, localMinV = FLT_MAX, localMinU2 = FLT_MAX, localMinV2 = FLT_MAX;
        float localMaxU = -FLT_MAX, localMaxV = -FLT_MAX, localMaxU2 = -FLT_MAX, localMaxV2 = -FLT_MAX;
        for (size_t index = sourceIndexStart; index < static_cast<size_t>(sourceIndexStart + sourceIndexCount); ++index)
        {
            const uint32_t srcIndex = sourceMesh.indices[index];
            if (srcIndex >= sourceMesh.vertices.size())
                return false;

            usedVertices.insert(srcIndex);
            const PrimitiveVertex& vertex = sourceMesh.vertices[srcIndex];
            localMinX = std::min(localMinX, vertex.x);
            localMinY = std::min(localMinY, vertex.y);
            localMinZ = std::min(localMinZ, vertex.z);
            localMaxX = std::max(localMaxX, vertex.x);
            localMaxY = std::max(localMaxY, vertex.y);
            localMaxZ = std::max(localMaxZ, vertex.z);
            localMinU = std::min(localMinU, vertex.u);
            localMinV = std::min(localMinV, vertex.v);
            localMinU2 = std::min(localMinU2, vertex.u2);
            localMinV2 = std::min(localMinV2, vertex.v2);
            localMaxU = std::max(localMaxU, vertex.u);
            localMaxV = std::max(localMaxV, vertex.v);
            localMaxU2 = std::max(localMaxU2, vertex.u2);
            localMaxV2 = std::max(localMaxV2, vertex.v2);
        }

        if (usedVertices.size() < 24)
            return appendOriginalSubmesh(sourceIndexStart, sourceIndexCount, sourceSubmesh);

        const float extentX = std::max(localMaxX - localMinX, 0.0001f);
        const float extentY = std::max(localMaxY - localMinY, 0.0001f);
        const float extentZ = std::max(localMaxZ - localMinZ, 0.0001f);
        const float longestExtent = std::max({extentX, extentY, extentZ});
        if (longestExtent <= 0.0001f)
            return appendOriginalSubmesh(sourceIndexStart, sourceIndexCount, sourceSubmesh);

        const float targetVertexCount = std::max(12.0f, static_cast<float>(usedVertices.size()) * targetRatio);
        const float cellsPerAxisHint = std::max(2.0f, std::cbrt(targetVertexCount));
        const int splitX = std::clamp(static_cast<int>(std::round(cellsPerAxisHint * (extentX / longestExtent))), 1, 48);
        const int splitY = std::clamp(static_cast<int>(std::round(cellsPerAxisHint * (extentY / longestExtent))), 1, 48);
        const int splitZ = std::clamp(static_cast<int>(std::round(cellsPerAxisHint * (extentZ / longestExtent))), 1, 48);
        const float cellSizeX = extentX / static_cast<float>(splitX);
        const float cellSizeY = extentY / static_cast<float>(splitY);
        const float cellSizeZ = extentZ / static_cast<float>(splitZ);

        const size_t vertexStartBefore = outMesh.vertices.size();
        const size_t indexStartBefore = outMesh.indices.size();

        Submesh outSubmesh = {};
        if (sourceSubmesh)
        {
            outSubmesh.materialPath = sourceSubmesh->materialPath;
            outSubmesh.texturePath = sourceSubmesh->texturePath;
            outSubmesh.indexStart = static_cast<uint32_t>(indexStartBefore);
        }

        std::unordered_map<ClusterKey, ClusterRepresentative, ClusterKeyHash> clusterToVertex;
        clusterToVertex.reserve(usedVertices.size());
        std::unordered_set<LodTriangleKey, LodTriangleKeyHash> seenTriangles;
        seenTriangles.reserve(sourceIndexCount / 3);

        auto getMappedVertex = [&](uint32_t srcIndex) -> uint32_t {
            const PrimitiveVertex& vertex = sourceMesh.vertices[srcIndex];
            const int cellX = std::min(splitX - 1, std::max(0, static_cast<int>((vertex.x - localMinX) / cellSizeX)));
            const int cellY = std::min(splitY - 1, std::max(0, static_cast<int>((vertex.y - localMinY) / cellSizeY)));
            const int cellZ = std::min(splitZ - 1, std::max(0, static_cast<int>((vertex.z - localMinZ) / cellSizeZ)));
            const ClusterKey key{
                cellX,
                cellY,
                cellZ,
                quantizeSignedUnit(vertex.nx),
                quantizeSignedUnit(vertex.ny),
                quantizeSignedUnit(vertex.nz),
                quantizeRange(vertex.u, localMinU, localMaxU, kUvBuckets),
                quantizeRange(vertex.v, localMinV, localMaxV, kUvBuckets),
                quantizeRange(vertex.u2, localMinU2, localMaxU2, kUvBuckets),
                quantizeRange(vertex.v2, localMinV2, localMaxV2, kUvBuckets),
            };

            auto [it, inserted] =
                clusterToVertex.emplace(key, ClusterRepresentative{static_cast<uint32_t>(outMesh.vertices.size()), FLT_MAX});
            if (inserted)
                outMesh.vertices.push_back(vertex);

            const float centerX = localMinX + (static_cast<float>(cellX) + 0.5f) * cellSizeX;
            const float centerY = localMinY + (static_cast<float>(cellY) + 0.5f) * cellSizeY;
            const float centerZ = localMinZ + (static_cast<float>(cellZ) + 0.5f) * cellSizeZ;
            const float dx = vertex.x - centerX;
            const float dy = vertex.y - centerY;
            const float dz = vertex.z - centerZ;
            const float distanceSq = dx * dx + dy * dy + dz * dz;

            ClusterRepresentative& representative = it->second;
            if (distanceSq < representative.distanceSq)
            {
                representative.distanceSq = distanceSq;
                outMesh.vertices[representative.newVertexIndex] = vertex;
            }
            return representative.newVertexIndex;
        };

        for (size_t index = sourceIndexStart; index + 2 < static_cast<size_t>(sourceIndexStart + sourceIndexCount); index += 3)
        {
            const uint32_t srcA = sourceMesh.indices[index + 0];
            const uint32_t srcB = sourceMesh.indices[index + 1];
            const uint32_t srcC = sourceMesh.indices[index + 2];
            if (srcA >= sourceMesh.vertices.size() || srcB >= sourceMesh.vertices.size() || srcC >= sourceMesh.vertices.size())
                return false;

            const uint32_t a = getMappedVertex(srcA);
            const uint32_t b = getMappedVertex(srcB);
            const uint32_t c = getMappedVertex(srcC);
            if (a == b || b == c || a == c)
                continue;

            LodTriangleKey dedupeKey{
                std::min({a, b, c}),
                a + b + c - std::min({a, b, c}) - std::max({a, b, c}),
                std::max({a, b, c})
            };
            if (!seenTriangles.insert(dedupeKey).second)
                continue;

            outMesh.indices.push_back(a);
            outMesh.indices.push_back(b);
            outMesh.indices.push_back(c);
            if (sourceSubmesh)
                outSubmesh.indexCount += 3;
        }

        const uint32_t resultingIndexCount =
            sourceSubmesh ? outSubmesh.indexCount : static_cast<uint32_t>(outMesh.indices.size() - indexStartBefore);
        if (resultingIndexCount == 0 || resultingIndexCount < sourceIndexCount / 6)
        {
            outMesh.vertices.resize(vertexStartBefore);
            outMesh.indices.resize(indexStartBefore);
            return appendOriginalSubmesh(sourceIndexStart, sourceIndexCount, sourceSubmesh);
        }

        if (sourceSubmesh && outSubmesh.indexCount > 0)
            outMesh.submeshes.push_back(std::move(outSubmesh));
        return true;
    };

    if (sourceMesh.submeshes.empty())
    {
        if (!appendDecimatedSubmesh(0, static_cast<uint32_t>(sourceMesh.indices.size()), nullptr))
            return false;
    }
    else
    {
        for (const Submesh& sourceSubmesh : sourceMesh.submeshes)
        {
            if (!appendDecimatedSubmesh(sourceSubmesh.indexStart, sourceSubmesh.indexCount, &sourceSubmesh))
                return false;
        }
    }

    if (outMesh.indices.size() < 12 || outMesh.indices.size() >= (sourceMesh.indices.size() * 98) / 100)
        return false;
    if (outMesh.vertices.size() >= (sourceMesh.vertices.size() * 98) / 100)
        return false;

    outMesh.boundsMinX = sourceMesh.boundsMinX;
    outMesh.boundsMinY = sourceMesh.boundsMinY;
    outMesh.boundsMinZ = sourceMesh.boundsMinZ;
    outMesh.boundsMaxX = sourceMesh.boundsMaxX;
    outMesh.boundsMaxY = sourceMesh.boundsMaxY;
    outMesh.boundsMaxZ = sourceMesh.boundsMaxZ;
    return true;
}

static void OptimizeMeshSubmeshesByMaterial(MeshData& meshData)
{
    if (meshData.submeshes.size() < 2 || meshData.indices.empty())
        return;

    struct GroupedSubmesh
    {
        Submesh submesh;
        std::vector<uint32_t> indices;
    };

    std::vector<GroupedSubmesh> groups;
    groups.reserve(meshData.submeshes.size());
    std::unordered_map<std::string, size_t> groupByMaterial;
    groupByMaterial.reserve(meshData.submeshes.size());

    auto makeKey = [](const Submesh& submesh) {
        std::string key;
        if (!submesh.texturePath.empty())
        {
            key.reserve(submesh.texturePath.size() + 8);
            key += "tex:";
            key += submesh.texturePath;
            return key;
        }

        key.reserve(submesh.materialPath.size() + 4);
        key += "mat:";
        key += submesh.materialPath;
        return key;
    };

    for (const Submesh& submesh : meshData.submeshes)
    {
        if (submesh.indexCount == 0 || submesh.indexStart >= meshData.indices.size() ||
            submesh.indexCount > meshData.indices.size() - submesh.indexStart)
        {
            continue;
        }

        const std::string key = makeKey(submesh);
        auto [it, inserted] = groupByMaterial.emplace(key, groups.size());
        if (inserted)
        {
            GroupedSubmesh group;
            group.submesh.materialPath = submesh.materialPath;
            group.submesh.texturePath = submesh.texturePath;
            groups.push_back(std::move(group));
            it = groupByMaterial.find(key);
        }

        GroupedSubmesh& group = groups[it->second];
        group.indices.insert(group.indices.end(), meshData.indices.begin() + submesh.indexStart,
                             meshData.indices.begin() + submesh.indexStart + submesh.indexCount);
    }

    if (groups.size() >= meshData.submeshes.size())
        return;

    std::vector<uint32_t> mergedIndices;
    mergedIndices.reserve(meshData.indices.size());
    std::vector<Submesh> mergedSubmeshes;
    mergedSubmeshes.reserve(groups.size());

    for (GroupedSubmesh& group : groups)
    {
        if (group.indices.empty())
            continue;

        group.submesh.indexStart = static_cast<uint32_t>(mergedIndices.size());
        group.submesh.indexCount = static_cast<uint32_t>(group.indices.size());
        mergedIndices.insert(mergedIndices.end(), group.indices.begin(), group.indices.end());
        mergedSubmeshes.push_back(std::move(group.submesh));
    }

    if (mergedSubmeshes.empty())
        return;

    meshData.indices = std::move(mergedIndices);
    meshData.submeshes = std::move(mergedSubmeshes);
}

static void BuildMeshRenderBatches(const std::vector<Submesh>& submeshes, uint32_t fullIndexCount,
                                   std::vector<PrimitiveMesh::RenderBatch>& outBatches, bool& outHasTexturedSubmeshes)
{
    outBatches.clear();
    outHasTexturedSubmeshes = false;

    if (submeshes.empty())
    {
        PrimitiveMesh::RenderBatch batch;
        batch.sections.push_back({0, fullIndexCount});
        outBatches.push_back(std::move(batch));
        return;
    }

    PrimitiveMesh::RenderBatch currentBatch;
    bool hasCurrentBatch = false;

    auto flushBatch = [&]() {
        if (hasCurrentBatch && !currentBatch.sections.empty())
        {
            outBatches.push_back(std::move(currentBatch));
            currentBatch = PrimitiveMesh::RenderBatch{};
            hasCurrentBatch = false;
        }
    };

    for (const Submesh& submesh : submeshes)
    {
        if (submesh.indexCount == 0)
            continue;

        if (!submesh.texturePath.empty())
            outHasTexturedSubmeshes = true;

        if (!hasCurrentBatch || currentBatch.texturePath != submesh.texturePath)
        {
            flushBatch();
            currentBatch.texturePath = submesh.texturePath;
            hasCurrentBatch = true;
        }

        currentBatch.sections.push_back({submesh.indexStart, submesh.indexCount});
    }

    flushBatch();
}

static void ResolveMeshRenderBatchTextures(const std::vector<Submesh>& sourceSubmeshes,
                                           std::vector<PrimitiveMesh::RenderBatch>& batches,
                                           const std::function<AssetHandle<TextureAsset>(const std::string&)>&
                                               resolveTextureHandle)
{
    if (batches.empty())
        return;

    if (sourceSubmeshes.empty())
    {
        for (PrimitiveMesh::RenderBatch& batch : batches)
        {
            if (!batch.texturePath.empty())
            {
                batch.textureHandle = resolveTextureHandle(batch.texturePath);
            }
        }
        return;
    }

    std::unordered_map<std::string, AssetHandle<TextureAsset>> resolvedByPath;
    resolvedByPath.reserve(sourceSubmeshes.size());

    for (const Submesh& submesh : sourceSubmeshes)
    {
        if (submesh.texturePath.empty() || resolvedByPath.contains(submesh.texturePath))
            continue;

        resolvedByPath.emplace(submesh.texturePath, resolveTextureHandle(submesh.texturePath));
    }

    for (PrimitiveMesh::RenderBatch& batch : batches)
    {
        if (batch.texturePath.empty())
            continue;

        auto it = resolvedByPath.find(batch.texturePath);
        if (it != resolvedByPath.end())
            batch.textureHandle = it->second;
    }
}

static float GetWorldMaxScale(const float* worldMatrix)
{
    if (!worldMatrix)
        return 1.0f;

    const float scaleX = std::sqrt(worldMatrix[0] * worldMatrix[0] + worldMatrix[1] * worldMatrix[1] +
                                   worldMatrix[2] * worldMatrix[2]);
    const float scaleY = std::sqrt(worldMatrix[4] * worldMatrix[4] + worldMatrix[5] * worldMatrix[5] +
                                   worldMatrix[6] * worldMatrix[6]);
    const float scaleZ = std::sqrt(worldMatrix[8] * worldMatrix[8] + worldMatrix[9] * worldMatrix[9] +
                                   worldMatrix[10] * worldMatrix[10]);
    return std::max({scaleX, scaleY, scaleZ, 0.0001f});
}

static float EstimateProjectedScreenHeight(const Camera& camera, const PrimitiveMesh& mesh, const float* worldMatrix)
{
    if (!worldMatrix)
        return 1.0f;

    float camX, camY, camZ;
    camera.GetPosition(camX, camY, camZ);

    const float dx = worldMatrix[12] - camX;
    const float dy = worldMatrix[13] - camY;
    const float dz = worldMatrix[14] - camZ;
    const float distance = std::max(std::sqrt(dx * dx + dy * dy + dz * dz), camera.GetNearZ());
    const float tanHalfFov = std::tan(camera.GetFOV() * 0.5f);
    if (tanHalfFov <= 0.0001f)
        return 1.0f;

    const float objectRadius = std::max(mesh.GetMaxExtent() * 0.5f * GetWorldMaxScale(worldMatrix), 0.001f);
    return std::clamp((objectRadius / distance) / tanHalfFov * 2.0f, 0.0f, 16.0f);
}

static uint32_t ChooseMeshLodLevel(const Camera& camera, const PrimitiveMesh& mesh, const float* worldMatrix)
{
    const float projectedHeight = EstimateProjectedScreenHeight(camera, mesh, worldMatrix);
    float lod1Threshold = 0.0f;
    float lod2Threshold = 0.0f;
    GetMeshLodScreenHeightThresholds(mesh, ViewSettings::Get().lodAggressiveness, lod1Threshold, lod2Threshold);
    if (projectedHeight < lod2Threshold && mesh.lodIndexCounts[2] > 0)
        return 2;
    if (projectedHeight < lod1Threshold && mesh.lodIndexCounts[1] > 0)
        return 1;
    return 0;
}

static bool HasGeneratedMeshLod(const PrimitiveMesh& mesh, uint32_t lodLevel)
{
    if (lodLevel == 0 || lodLevel >= PrimitiveMesh::kLodCount)
        return false;
    return mesh.lodVertexBuffers[lodLevel] && mesh.lodIndexBuffers[lodLevel] &&
           (mesh.lodVertexBuffers[lodLevel] != mesh.vertexBuffer || mesh.lodIndexBuffers[lodLevel] != mesh.indexBuffer ||
            mesh.lodIndexCounts[lodLevel] != mesh.indexCount);
}

static void SetLodDebugTint(float* tint, bool enabled, uint32_t lodLevel)
{
    if (!tint)
        return;

    tint[0] = 0.0f;
    tint[1] = 0.0f;
    tint[2] = 0.0f;

    if (!enabled)
        return;

    if (lodLevel == 0)
    {
        tint[0] = 0.18f;
        tint[1] = 0.85f;
        tint[2] = 0.22f;
    }
    else if (lodLevel == 1)
    {
        tint[0] = 1.0f;
        tint[1] = 0.75f;
        tint[2] = 0.0f;
    }
    else
    {
        tint[0] = 1.0f;
        tint[1] = 0.0f;
        tint[2] = 0.25f;
    }
}

static void RecordLodDraw(uint32_t lodLevel)
{
    if (lodLevel >= PrimitiveMesh::kLodCount)
        lodLevel = 0;

    if (lodLevel == 2)
        ++g_RenderDebugStats.lod2Draws;
    else if (lodLevel == 1)
        ++g_RenderDebugStats.lod1Draws;
    else
        ++g_RenderDebugStats.lod0Draws;
}

static const char* s_SharedLightingBufferHLSL = R"(
    struct PointLight {
        float3 position; float range;
        float3 color; float intensity;
        float shadowEnabled; float shadowBaseSlice; float shadowBias; float _pad;
    };

    struct SpotLight {
        float3 position; float range;
        float3 direction; float innerCos;
        float3 color; float outerCos;
        float intensity; float shadowEnabled; float shadowBaseSlice; float shadowBias;
    };

    struct GPULight {
        float3 position;
        float range;
        float3 color;
        float intensity;
        uint type;
        float3 direction;
        float spotAngle;
        float shadowEnabled;
        float shadowBaseSlice;
        float shadowBias;
    };

    StructuredBuffer<GPULight> AllLights : register(t7);
    Buffer<uint> LightGrid : register(t8);
    Buffer<uint> LightIndices : register(t9);
    TextureCube ReflectionProbeTex0 : register(t10);
    TextureCube ReflectionProbeTex1 : register(t11);
    SamplerState ReflectionProbeSampler : register(s6);

    cbuffer ConstantBuffer : register(b0)
    {
        float4x4 MVP;
        float4x4 Model;
        float3 DirLightDir;
        float DirLightIntensity;
        float3 DirLightColor;
        float _pad1;
        float3 AmbientColor;
        float AmbientIntensity;
        int NumPointLights;
        int NumSpotLights;
        float2 _pad2;
        PointLight PointLights[16];
        SpotLight SpotLights[16];

        float3 MaterialColor;
        float MaterialMetallic;

        float MaterialRoughness;
        float3 MaterialEmissiveColor;

        float MaterialEmissiveStrength;
        float3 LodDebugTint;

        float4 HasTextures;
        float AlbedoTextureSlot;
        float NormalTextureSlot;
        float OrmTextureSlot;
        float _PadTextureSlots;
        float2 UVTiling;
        float2 UVOffset;

        float3 CameraPos;
        float _Time;
        float2 PannerSpeed;
        int PannerMethod;
        int PannerLink;

        float2 LightmapUVScale;
        float2 LightmapUVOffset;

        float LightmapEnabled;
        float LightmapTextureSlot;
        float LightmapIntensity;
        float MaterialAmbientOcclusion;

        float4 ReflectionProbePositionRadius[2];
        float4 ReflectionProbeTintIntensity[2];
        float4 ReflectionProbeParams[2];
        float4 ReflectionProbeBoxExtents[2];

        float4x4 ShadowMatrix;
        float ShadowBias;
        float ShadowEnabled;
        float ViewportWidth;
        float ViewportHeight;
        float fPlusEnabled;
        uint tileCountX;
        uint tileCountY;
        uint debugVisMode;
        float SSAOEnabled;
        float3 _Pad3;
    };

    float DotPow5(float x)
    {
        float x2 = x * x;
        return x2 * x2 * x;
    }

    float3 DotFresnelSchlick(float cosTheta, float3 F0)
    {
        return F0 + (1.0f - F0) * DotPow5(1.0f - saturate(cosTheta));
    }

    float3 DotFresnelSchlickRoughness(float cosTheta, float3 F0, float roughness)
    {
        float oneMinusRoughness = 1.0f - roughness;
        float3 grazing = max(float3(oneMinusRoughness, oneMinusRoughness, oneMinusRoughness), F0);
        return F0 + (grazing - F0) * DotPow5(1.0f - saturate(cosTheta));
    }

    float DotDistributionGGX(float NdotH, float roughness)
    {
        float a = max(roughness * roughness, 0.045f);
        float a2 = a * a;
        float denom = NdotH * NdotH * (a2 - 1.0f) + 1.0f;
        return a2 / max(3.14159265f * denom * denom, 1e-4f);
    }

    float DotGeometrySchlickGGX(float NdotX, float roughness)
    {
        float r = roughness + 1.0f;
        float k = (r * r) * 0.125f;
        return NdotX / max(NdotX * (1.0f - k) + k, 1e-4f);
    }

    float DotGeometrySmith(float NdotV, float NdotL, float roughness)
    {
        return DotGeometrySchlickGGX(saturate(NdotV), roughness) *
               DotGeometrySchlickGGX(saturate(NdotL), roughness);
    }

    float3 DotEvaluateSpecularBRDF(float3 F0, float roughness, float NdotV, float NdotL, float NdotH, float VdotH)
    {
        float3 F = DotFresnelSchlick(VdotH, F0);
        float D = DotDistributionGGX(NdotH, roughness);
        float G = DotGeometrySmith(NdotV, NdotL, roughness);
        return (D * G * F) / max(4.0f * max(NdotV, 1e-4f) * max(NdotL, 1e-4f), 1e-4f);
    }

    float3 DotComputeSkylightDiffuse(float3 diffuseAlbedo, float3 normalWS, float ambientOcclusion)
    {
        float hemi = saturate(normalWS.y * 0.5f + 0.5f);
        float horizon = 1.0f - abs(normalWS.y);
        float3 baseAmbient = AmbientColor * AmbientIntensity;
        float3 skyColor = baseAmbient * 1.35f + DirLightColor * (DirLightIntensity * 0.05f);
        float3 groundColor = baseAmbient * 0.30f;
        float3 horizonColor = baseAmbient * 0.70f;
        float3 irradiance = lerp(groundColor, skyColor, hemi);
        irradiance = lerp(irradiance, horizonColor, horizon * 0.35f);
        return diffuseAlbedo * irradiance * ambientOcclusion;
    }

    float3 DotComputeSkylightSpecular(float3 F0, float roughness, float3 normalWS, float3 viewDir, float ambientOcclusion)
    {
        float3 reflected = reflect(-viewDir, normalWS);
        float hemi = saturate(reflected.y * 0.5f + 0.5f);
        float horizon = 1.0f - abs(reflected.y);
        float3 baseAmbient = AmbientColor * AmbientIntensity;
        float3 skyColor = baseAmbient * 1.35f + DirLightColor * (DirLightIntensity * 0.05f);
        float3 groundColor = baseAmbient * 0.25f;
        float3 horizonColor = baseAmbient * 0.60f;
        float3 envColor = lerp(groundColor, skyColor, hemi);
        envColor = lerp(envColor, horizonColor, horizon * 0.40f);
        float NdotV = saturate(dot(normalWS, viewDir));
        float3 envF = DotFresnelSchlickRoughness(NdotV, F0, roughness);
        float glossy = saturate(1.0f - roughness);
        float specStrength = lerp(0.04f, 0.55f, glossy * glossy);
        float specOcclusion = lerp(ambientOcclusion * 0.55f, ambientOcclusion, glossy);
        return envColor * envF * specStrength * specOcclusion;
    }

    float3 DotRotateY(float3 direction, float angleDegrees)
    {
        float radians = angleDegrees * (3.14159265f / 180.0f);
        float s = sin(radians);
        float c = cos(radians);
        return float3(direction.x * c - direction.z * s, direction.y, direction.x * s + direction.z * c);
    }

    float DotComputeReflectionProbeInfluence(float3 worldPos, float3 probePosition, float3 probeBoxExtents,
                                             float probeRotation, float probeFalloff, float probeBlendWeight)
    {
        if (probeBlendWeight <= 0.0f)
            return 0.0f;

        float3 localPos = worldPos - probePosition;
        if (abs(probeRotation) > 0.001f)
            localPos = DotRotateY(localPos, -probeRotation);

        float3 safeExtents = max(probeBoxExtents, float3(0.001f, 0.001f, 0.001f));
        float3 normalizedDistance = abs(localPos) / safeExtents;
        float edge = max(normalizedDistance.x, max(normalizedDistance.y, normalizedDistance.z));
        if (edge >= 1.0f)
            return 0.0f;

        float edgeFraction = saturate(probeFalloff);
        if (edgeFraction <= 0.0001f)
            return probeBlendWeight;

        float fadeStart = 1.0f - edgeFraction;
        float probeWeight = edge <= fadeStart ? 1.0f : (1.0f - smoothstep(fadeStart, 1.0f, edge));
        return probeWeight * probeBlendWeight;
    }

    float3 DotParallaxCorrectBoxReflection(float3 worldPos, float3 reflectedDir, float3 probePosition,
                                           float3 probeBoxExtents, float probeRotation)
    {
        float3 localPos = worldPos - probePosition;
        float3 rayDir = normalize(reflectedDir);
        if (abs(probeRotation) > 0.001f)
        {
            localPos = DotRotateY(localPos, -probeRotation);
            rayDir = DotRotateY(rayDir, -probeRotation);
        }

        float3 safeExtents = max(probeBoxExtents, float3(0.001f, 0.001f, 0.001f));
        float3 safeRayDir = rayDir;
        safeRayDir.x = abs(safeRayDir.x) < 1e-4f ? (safeRayDir.x >= 0.0f ? 1e-4f : -1e-4f) : safeRayDir.x;
        safeRayDir.y = abs(safeRayDir.y) < 1e-4f ? (safeRayDir.y >= 0.0f ? 1e-4f : -1e-4f) : safeRayDir.y;
        safeRayDir.z = abs(safeRayDir.z) < 1e-4f ? (safeRayDir.z >= 0.0f ? 1e-4f : -1e-4f) : safeRayDir.z;

        float3 tMin = (-safeExtents - localPos) / safeRayDir;
        float3 tMax = (safeExtents - localPos) / safeRayDir;
        float3 t1 = min(tMin, tMax);
        float3 t2 = max(tMin, tMax);
        float tNear = max(t1.x, max(t1.y, t1.z));
        float tFar = min(t2.x, min(t2.y, t2.z));
        if (tNear > tFar || tFar <= 0.0f)
            return rayDir;

        return normalize(localPos + rayDir * tFar);
    }

    float3 DotComputeReflectionProbeSpecular(float3 worldPos, float3 F0, float roughness, float3 normalWS,
                                             float3 viewDir, float ambientOcclusion, out float probeCoverage)
    {
        float probeMip = roughness * 5.0f;
        float NdotV = saturate(dot(normalWS, viewDir));
        float3 envF = DotFresnelSchlickRoughness(NdotV, F0, roughness);
        float glossy = saturate(1.0f - roughness);
        float specStrength = lerp(0.08f, 1.0f, glossy * glossy);
        float roughnessDamp = lerp(0.35f, 1.0f, glossy);
        float specOcclusion = lerp(ambientOcclusion * 0.55f, ambientOcclusion, glossy);
        float3 reflected = reflect(-viewDir, normalWS);
        float3 totalProbeColor = float3(0.0f, 0.0f, 0.0f);
        probeCoverage = 0.0f;

        [unroll]
        for (int probeIndex = 0; probeIndex < 2; ++probeIndex)
        {
            float3 probePosition = ReflectionProbePositionRadius[probeIndex].xyz;
            float probeRadius = ReflectionProbePositionRadius[probeIndex].w;
            float3 probeTint = ReflectionProbeTintIntensity[probeIndex].xyz;
            float probeIntensity = ReflectionProbeTintIntensity[probeIndex].w;
            float probeFalloff = ReflectionProbeParams[probeIndex].x;
            float probeRotation = ReflectionProbeParams[probeIndex].y;
            float probeBlendWeight = ReflectionProbeParams[probeIndex].z;
            float probeEnabled = ReflectionProbeParams[probeIndex].w;
            float3 probeBoxExtents = ReflectionProbeBoxExtents[probeIndex].xyz;

            if (probeEnabled < 0.5f || probeRadius <= 0.0f)
                continue;

            float probeWeight = DotComputeReflectionProbeInfluence(worldPos, probePosition, probeBoxExtents,
                                                                  probeRotation, probeFalloff, probeBlendWeight);
            if (probeWeight <= 0.0001f)
                continue;

            float3 correctedReflection =
                DotParallaxCorrectBoxReflection(worldPos, reflected, probePosition, probeBoxExtents, probeRotation);

            float3 probeColor = (probeIndex == 0)
                                    ? ReflectionProbeTex0.SampleLevel(ReflectionProbeSampler, correctedReflection, probeMip).rgb
                                    : ReflectionProbeTex1.SampleLevel(ReflectionProbeSampler, correctedReflection, probeMip).rgb;
            totalProbeColor += probeColor * probeTint * probeIntensity * probeWeight;
            probeCoverage += probeWeight;
        }

        probeCoverage = saturate(probeCoverage);
        return totalProbeColor * envF * specStrength * roughnessDamp * specOcclusion;
    }
)";

static const char* s_NormalMapHelpersTemplateHLSL = R"(

    float4 SampleMaterialTexture(int slot, float2 uv)
    {
        if (slot <= 0) return tex0.Sample(sampler0, uv);
        if (slot == 1) return tex1.Sample(sampler1, uv);
        if (slot == 2) return tex2.Sample(sampler2, uv);
        return tex3.Sample(sampler3, uv);
    }

    float3 DotBuildFallbackTangent(float3 N)
    {
        float3 referenceAxis = abs(N.z) < 0.999f ? float3(0.0f, 0.0f, 1.0f) : float3(0.0f, 1.0f, 0.0f);
        return normalize(cross(referenceAxis, N));
    }

    float3 DotDecodeNormalSample(float3 encodedNormal, float2 uv, float3 worldPos, float3 worldNormal, float4 worldTangent)
    {
        float3 N = normalize(worldNormal);
        float3 T = float3(0.0f, 0.0f, 0.0f);
        float3 B = float3(0.0f, 0.0f, 0.0f);

        float tangentLenSq = dot(worldTangent.xyz, worldTangent.xyz);
        if (tangentLenSq > 1e-6f)
        {
            T = worldTangent.xyz - N * dot(N, worldTangent.xyz);
            float orthoLenSq = dot(T, T);
            if (orthoLenSq > 1e-8f)
            {
                T *= rsqrt(orthoLenSq);
                float tangentSign = worldTangent.w < 0.0f ? -1.0f : 1.0f;
                B = normalize(cross(N, T)) * tangentSign;
            }
        }

        if (dot(T, T) < 1e-6f || dot(B, B) < 1e-6f)
        {
            // Fine derivatives reduce 2x2-quad blending artifacts at triangle edges.
            float3 dpdx = ddx_fine(worldPos);
            float3 dpdy = ddy_fine(worldPos);
            float2 duvdx = ddx_fine(uv);
            float2 duvdy = ddy_fine(uv);
            float3 dp2perp = cross(dpdy, N);
            float3 dp1perp = cross(N, dpdx);
            T = dp2perp * duvdx.x + dp1perp * duvdy.x;
            B = dp2perp * duvdx.y + dp1perp * duvdy.y;
            float tLenSq = dot(T, T);
            float bLenSq = dot(B, B);
            float maxLenSq = max(tLenSq, bLenSq);
            if (maxLenSq > 1e-10f)
            {
                float invMaxLen = rsqrt(maxLenSq);
                T *= invMaxLen;
                B *= invMaxLen;
                T = normalize(T - N * dot(N, T));
                B = normalize(B - N * dot(N, B));
                if (dot(cross(T, B), N) < 0.0f)
                    B = -B;
            }
            else
            {
                T = DotBuildFallbackTangent(N);
                B = normalize(cross(N, T));
            }
        }

        float2 xy = encodedNormal.xy * 2.0f - 1.0f;
        float z = sqrt(saturate(1.0f - dot(xy, xy)));
        float3 tangentNormal = normalize(float3(xy, z));
        return normalize(T * tangentNormal.x + B * tangentNormal.y + N * tangentNormal.z);
    }

    float3 DotDecodeNormalSample(float3 encodedNormal, float2 uv, float3 worldPos, float3 worldNormal)
    {
        return DotDecodeNormalSample(encodedNormal, uv, worldPos, worldNormal, float4(0.0f, 0.0f, 0.0f, 1.0f));
    }
)";

static const std::string s_SimplePSTemplate = std::string(R"(
    Texture2D AlbedoTex : register(t0);
    SamplerState AlbedoSampler : register(s0);

    // Material graph texture slots (aliases for procedural materials)
    Texture2D tex0 : register(t0);
    Texture2D tex1 : register(t1);
    Texture2D tex2 : register(t2);
    Texture2D tex3 : register(t3);
    SamplerState sampler0 : register(s0);
    SamplerState sampler1 : register(s1);
    SamplerState sampler2 : register(s2);
    SamplerState sampler3 : register(s3);

    // Shadow mapping
    Texture2D ShadowMap : register(t4);
    SamplerComparisonState ShadowSampler : register(s4);

    // SSAO
    Texture2D SSAOTex : register(t5);
    Texture2DArray LocalShadowMap : register(t6);
    SamplerState ShadowSamplerPoint : register(s5);
    SamplerState SSAOSampler : register(s7);
)") + s_SharedLightingBufferHLSL + s_NormalMapHelpersTemplateHLSL + std::string(R"(

    struct PSInput { 
        float4 Position : SV_POSITION; 
        float3 WorldPos : TEXCOORD0;
        float3 WorldNormal : NORMAL;
        float4 WorldTangent : TANGENT;
        float4 Color : COLOR;
        float2 UV : TEXCOORD1;
        float2 UV2 : TEXCOORD2;
    };

    float CalcSpecular(float3 N, float3 L, float3 V, float roughness) {
        float3 H = normalize(L + V);
        float NdotH = max(dot(N, H), 0.0f);
        float shininess = max(2.0f, (1.0f - roughness) * 128.0f);
        return pow(NdotH, shininess);
    }

    // Shadow sampling function with OOB detection
    float SampleShadow(float3 worldPos) {
        float4 shadowCoord = mul(ShadowMatrix, float4(worldPos, 1.0f));
        shadowCoord.xyz /= shadowCoord.w;
        shadowCoord.xy = shadowCoord.xy * 0.5f + 0.5f;
        shadowCoord.y = 1.0f - shadowCoord.y;
        if (shadowCoord.x < 0 || shadowCoord.x > 1 || shadowCoord.y < 0 || shadowCoord.y > 1 || shadowCoord.z < 0 || shadowCoord.z > 1)
            return 2.0; // OOB marker
        float depth = shadowCoord.z - ShadowBias;
        return ShadowMap.SampleCmpLevelZero(ShadowSampler, shadowCoord.xy, depth);
    }
)") + std::string(R"(

    void GetPointLightFaceBasis(int faceIndex, out float3 forward, out float3 up)
    {
        if (faceIndex == 0) { forward = float3(1, 0, 0); up = float3(0, 1, 0); return; }
        if (faceIndex == 1) { forward = float3(-1, 0, 0); up = float3(0, 1, 0); return; }
        if (faceIndex == 2) { forward = float3(0, 1, 0); up = float3(0, 0, -1); return; }
        if (faceIndex == 3) { forward = float3(0, -1, 0); up = float3(0, 0, 1); return; }
        if (faceIndex == 4) { forward = float3(0, 0, 1); up = float3(0, 1, 0); return; }
        forward = float3(0, 0, -1); up = float3(0, 1, 0);
    }

    int SelectPointLightShadowFace(float3 lightToPoint)
    {
        float3 absVec = abs(lightToPoint);
        if (absVec.x >= absVec.y && absVec.x >= absVec.z)
            return lightToPoint.x >= 0.0f ? 0 : 1;
        if (absVec.y >= absVec.x && absVec.y >= absVec.z)
            return lightToPoint.y >= 0.0f ? 2 : 3;
        return lightToPoint.z >= 0.0f ? 4 : 5;
    }

    float SamplePointLightShadow(PointLight light, float3 worldPos)
    {
        if (light.shadowEnabled < 0.5f)
            return 1.0f;

        float3 lightToPoint = worldPos - light.position;
        int faceIndex = SelectPointLightShadowFace(lightToPoint);
        float3 forward, up;
        GetPointLightFaceBasis(faceIndex, forward, up);
        float3 right = normalize(cross(up, forward));
        float3 localUp = cross(forward, right);

        float faceZ = dot(lightToPoint, forward);
        if (faceZ <= 0.0f)
            return 1.0f;

        float faceX = dot(lightToPoint, right);
        float faceY = dot(lightToPoint, localUp);
        float2 uv = float2(faceX / faceZ, -faceY / faceZ) * 0.5f + 0.5f;
        if (uv.x < 0.0f || uv.x > 1.0f || uv.y < 0.0f || uv.y > 1.0f)
            return 1.0f;

        const float nearPlane = 0.05f;
        const float farPlane = max(light.range, nearPlane + 0.001f);
        float compareDepth = (farPlane / (farPlane - nearPlane)) - ((nearPlane * farPlane) / ((farPlane - nearPlane) * faceZ));
        compareDepth -= light.shadowBias;

        const float texel = 1.0f / 1024.0f;
        float visibility = 0.0f;
        [unroll]
        for (int y = -1; y <= 1; ++y)
        {
            [unroll]
            for (int x = -1; x <= 1; ++x)
            {
                float2 sampleUV = uv + float2(x, y) * texel;
                float sampledDepth = LocalShadowMap.SampleLevel(ShadowSamplerPoint, float3(sampleUV, light.shadowBaseSlice + faceIndex), 0).r;
                visibility += compareDepth <= sampledDepth ? 1.0f : 0.0f;
            }
        }

        float shadow = visibility / 9.0f;
        shadow = saturate((shadow - 0.15f) / 0.85f);
        return shadow * shadow;
    }
)") + std::string(R"(

    float SampleSpotLightShadow(SpotLight light, float3 worldPos) {
        if (light.shadowEnabled < 0.5f) return 1.0f;
        float3 f = normalize(light.direction), p = worldPos - light.position;
        float z = dot(p, f); if (z <= 0.0f) return 1.0f;
        float3 refUp = abs(f.y) > 0.99f ? float3(0,0,1) : float3(0,1,0);
        float3 r = normalize(cross(refUp, f)), u = cross(f, r);
        float t = tan(max(acos(saturate(light.outerCos)), 0.0174533f));
        float2 uv = float2(dot(p, r) / (z * t), -dot(p, u) / (z * t)) * 0.5f + 0.5f;
        if (uv.x < 0.0f || uv.x > 1.0f || uv.y < 0.0f || uv.y > 1.0f) return 1.0f;
        const float n = 0.05f, fa = max(light.range, n + 0.001f), texel = 1.0f / 1024.0f;
        float d = (fa / (fa - n)) - ((n * fa) / ((fa - n) * z)) - light.shadowBias, vis = 0.0f;
        [unroll] for (int y = -1; y <= 1; ++y) { [unroll] for (int x = -1; x <= 1; ++x) {
            float sd = LocalShadowMap.SampleLevel(ShadowSamplerPoint, float3(uv + float2(x, y) * texel, light.shadowBaseSlice), 0).r;
            vis += d <= sd ? 1.0f : 0.0f; } }
        float s = saturate(((vis / 9.0f) - 0.15f) / 0.85f); return s * s;
    }

    // --- Injection Point ---
#ifdef MATERIAL_SURFACE_CUSTOM
    void GetMaterialSurface(float2 uv, float3 worldPos, float3 worldNormal, float4 worldTangent, inout float3 albedo, inout float metallic, inout float roughness, inout float ao, inout float3 normal);
#else
    void GetMaterialSurface(float2 uv, float3 worldPos, float3 worldNormal, float4 worldTangent, inout float3 albedo, inout float metallic, inout float roughness, inout float ao, inout float3 normal)
    {
        float2 panSpeed = PannerSpeed;
        if (PannerLink > 0) panSpeed.y = panSpeed.x;
        
        float2 uvInput = uv * UVTiling + UVOffset;
        float2 texUV = uvInput;
        
        if (PannerMethod == 0) // Linear
            texUV = uvInput + _Time * panSpeed;
        else if (PannerMethod == 1) // Sine
            texUV = uvInput + panSpeed * sin(_Time);
        else if (PannerMethod == 2) // ZigZag
            texUV = uvInput + panSpeed * (abs(frac(_Time * 0.5f) * 2.0f - 1.0f) * 2.0f - 1.0f);
        else if (PannerMethod == 3) // Rotate
        {
            float s, c; sincos(_Time * panSpeed.x, s, c);
            texUV = mul(uvInput - 0.5f, float2x2(c, -s, s, c)) + 0.5f;
        }

        int albedoSlot = (int)(AlbedoTextureSlot + 0.5f);
        if (AlbedoTextureSlot > -0.5f && albedoSlot >= 0 && albedoSlot < 4 && HasTextures[albedoSlot] > 0.5f)
            albedo = SampleMaterialTexture(albedoSlot, texUV).rgb;

        int normalSlot = (int)(NormalTextureSlot + 0.5f);
        if (NormalTextureSlot > -0.5f && normalSlot >= 0 && normalSlot < 4 && HasTextures[normalSlot] > 0.5f)
            normal = DotDecodeNormalSample(SampleMaterialTexture(normalSlot, texUV).xyz, texUV, worldPos, worldNormal, worldTangent);

        int ormSlot = (int)(OrmTextureSlot + 0.5f);
        if (OrmTextureSlot > -0.5f && ormSlot >= 0 && ormSlot < 4 && HasTextures[ormSlot] > 0.5f)
        {
            float3 orm = SampleMaterialTexture(ormSlot, texUV).rgb;
            ao = saturate(orm.r);
            roughness = saturate(orm.g);
            metallic = saturate(orm.b);
        }
    }

    float3 SampleLightmap(float2 uv)
    {
        if (LightmapEnabled < 0.5f || LightmapTextureSlot < 0.5f)
            return float3(0.0f, 0.0f, 0.0f);

        const float2 lightmapUv = uv * LightmapUVScale + LightmapUVOffset;
        if (LightmapTextureSlot < 1.5f)
            return tex1.Sample(AlbedoSampler, lightmapUv).rgb * LightmapIntensity;
        if (LightmapTextureSlot < 2.5f)
            return tex2.Sample(AlbedoSampler, lightmapUv).rgb * LightmapIntensity;
        return tex3.Sample(AlbedoSampler, lightmapUv).rgb * LightmapIntensity;
    }
#endif

    float4 PSMain(PSInput input) : SV_TARGET { 
        float3 albedo = MaterialColor;
        float metallic = MaterialMetallic;
        float roughness = MaterialRoughness;
        float ao = MaterialAmbientOcclusion;
        float3 emissive = MaterialEmissiveColor * MaterialEmissiveStrength;
        float3 normal = normalize(input.WorldNormal);

        GetMaterialSurface(input.UV, input.WorldPos, input.WorldNormal, input.WorldTangent, albedo, metallic, roughness, ao, normal);

        metallic = saturate(metallic);
        roughness = clamp(roughness, 0.045f, 1.0f);
        albedo = saturate(albedo);

        if (debugVisMode == 1u) // Unlit
            return float4(albedo, 1.0f);
        if (debugVisMode == 6u) // Normals
            return float4(normalize(normal) * 0.5f + 0.5f, 1.0f);
        if (debugVisMode == 8u) // BaseColor
            return float4(albedo, 1.0f);
        if (debugVisMode == 9u) // Metallic
            return float4(metallic, metallic, metallic, 1.0f);
        if (debugVisMode == 10u) // Roughness
            return float4(roughness, roughness, roughness, 1.0f);

        float3 shadingAlbedo = debugVisMode == 4u ? float3(1.0f, 1.0f, 1.0f) : albedo;
        float3 macroN = normalize(input.WorldNormal);
        float3 N = normalize(normal);
        float3 V = normalize(CameraPos - input.WorldPos);
        
        // Sample SSAO only when the screen-space AO pass is actually available.
        float ssao = 1.0f;
        if (SSAOEnabled > 0.5f && ViewportWidth > 0.0f && ViewportHeight > 0.0f)
        {
            float2 screenUV = input.Position.xy / float2(ViewportWidth, ViewportHeight);
            ssao = saturate(SSAOTex.Sample(SSAOSampler, screenUV).r);
        }

        float aoFactor = lerp(1.0f, saturate(ao), 0.90f);
        float ambientOcclusion = max(lerp(1.0f, ssao, 0.85f), 0.18f) * aoFactor;
        float contactDiffuseOcclusion = lerp(1.0f, ambientOcclusion, 0.35f);
        float contactSpecularOcclusion = lerp(1.0f, ambientOcclusion, 0.20f);
        float NdotV = max(dot(N, V), 0.0f);
        float3 diffuseAlbedo = shadingAlbedo * (1.0f - metallic);
        float3 F0 = lerp(float3(0.04f, 0.04f, 0.04f), shadingAlbedo, metallic);
        float3 totalDiffuse = DotComputeSkylightDiffuse(diffuseAlbedo, N, ambientOcclusion);
        float probeCoverage = 0.0f;
        float3 probeSpecular =
            DotComputeReflectionProbeSpecular(input.WorldPos, F0, roughness, N, V, ambientOcclusion, probeCoverage);
        float3 skySpecular = DotComputeSkylightSpecular(F0, roughness, N, V, ambientOcclusion);
        float3 totalSpecular = skySpecular * (1.0f - probeCoverage) + probeSpecular;
        
        // Directional light with shadow
        if (DirLightIntensity > 0.0f) {
            float3 L = normalize(-DirLightDir);
            float rawMacroNdotL = dot(macroN, L);
            float macroLightGate = saturate((rawMacroNdotL + 0.12f) / 0.36f);
            float NdotL = max(dot(N, L), 0.0f) * macroLightGate;
            
            // Sample shadow
            float shadow = 1.0;
            if (ShadowEnabled > 0.5f && rawMacroNdotL > 0.0f) {
                shadow = SampleShadow(input.WorldPos);
            }
            
            float shadowFactor = (shadow > 1.5f) ? 1.0f : shadow;
            float3 radiance = DirLightColor * DirLightIntensity * shadowFactor;
            totalDiffuse += diffuseAlbedo * radiance * NdotL * contactDiffuseOcclusion;
            
            if (NdotL > 0.0f) {
                float3 H = normalize(L + V);
                float NdotH = max(dot(N, H), 0.0f);
                float VdotH = max(dot(V, H), 0.0f);
                totalSpecular += DotEvaluateSpecularBRDF(F0, roughness, NdotV, NdotL, NdotH, VdotH) * radiance * NdotL *
                                 contactSpecularOcclusion;
            }
        }
        
        if (fPlusEnabled > 0.5f) {
            uint2 pixelPos = uint2(input.Position.xy);
            uint2 tileIdx = pixelPos / 16;
            uint tileFlatIdx = tileIdx.y * tileCountX + tileIdx.x;
            
            uint offset = LightGrid[tileFlatIdx * 2];
            uint count = LightGrid[tileFlatIdx * 2 + 1];
            
            for (uint i = 0; i < count; i++) {
                uint lightIdx = LightIndices[offset + i];
                GPULight l = AllLights[lightIdx];
                
                float3 lightVec = l.position - input.WorldPos;
                float dist = length(lightVec);
                if (dist < l.range) {
                    float3 L = lightVec / dist;
                    float attenuation = pow(1.0f - saturate(dist / l.range), 2.0);
                    float visibility = 1.0;
                    
                    if (l.type == 1) { // Spot
                        float theta = dot(L, normalize(-l.direction));
                        visibility = saturate((theta - l.spotAngle) / 0.1f);
                    }

                    if (l.type == 0 && l.shadowEnabled > 0.5f) {
                        PointLight pointShadowLight;
                        pointShadowLight.position = l.position;
                        pointShadowLight.range = l.range;
                        pointShadowLight.color = l.color;
                        pointShadowLight.intensity = l.intensity;
                        pointShadowLight.shadowEnabled = l.shadowEnabled;
                        pointShadowLight.shadowBaseSlice = l.shadowBaseSlice;
                        pointShadowLight.shadowBias = l.shadowBias;
                        pointShadowLight._pad = 0.0f;
                        visibility *= SamplePointLightShadow(pointShadowLight, input.WorldPos);
                    } else if (l.type == 1 && l.shadowEnabled > 0.5f) {
                        SpotLight s; s.position = l.position; s.range = l.range; s.direction = l.direction; s.innerCos = 0.0f;
                        s.color = l.color; s.outerCos = l.spotAngle; s.intensity = l.intensity;
                        s.shadowEnabled = l.shadowEnabled; s.shadowBaseSlice = l.shadowBaseSlice; s.shadowBias = l.shadowBias;
                        visibility *= SampleSpotLightShadow(s, input.WorldPos);
                    }
                    
                    float3 lightContrib = l.color * l.intensity * attenuation * visibility;
                    float NdotL = max(dot(N, L), 0.0f);
                    totalDiffuse += diffuseAlbedo * lightContrib * NdotL * contactDiffuseOcclusion;
                    
                    if (NdotL > 0.0f) {
                        float3 H = normalize(L + V);
                        float NdotH = max(dot(N, H), 0.0f);
                        float VdotH = max(dot(V, H), 0.0f);
                        totalSpecular += DotEvaluateSpecularBRDF(F0, roughness, NdotV, NdotL, NdotH, VdotH) *
                                         lightContrib * NdotL * contactSpecularOcclusion;
                    }
                }
            }
        } else {
            // Point lights
            for (int i = 0; i < NumPointLights && i < 16; i++) {
                float3 lightVec = PointLights[i].position - input.WorldPos;
                float dist = length(lightVec);
                if (dist < PointLights[i].range) {
                    float3 L = lightVec / dist;
                    float attenuation = pow(1.0f - saturate(dist / PointLights[i].range), 2.0);
                    float shadow = 1.0f;
                    if (PointLights[i].shadowEnabled > 0.5f && max(dot(N, L), 0.0f) > 0.0f) {
                        shadow = SamplePointLightShadow(PointLights[i], input.WorldPos);
                    }
                    float3 lightContrib = PointLights[i].color * PointLights[i].intensity * attenuation * shadow;
                    float NdotL = max(dot(N, L), 0.0f);
                    totalDiffuse += diffuseAlbedo * lightContrib * NdotL * contactDiffuseOcclusion;
                    
                    if (NdotL > 0.0f) {
                        float3 H = normalize(L + V);
                        float NdotH = max(dot(N, H), 0.0f);
                        float VdotH = max(dot(V, H), 0.0f);
                        totalSpecular += DotEvaluateSpecularBRDF(F0, roughness, NdotV, NdotL, NdotH, VdotH) *
                                         lightContrib * NdotL * contactSpecularOcclusion;
                    }
                }
            }
            
            // Spot lights
            for (int j = 0; j < NumSpotLights && j < 16; j++) {
                float3 lightVec = SpotLights[j].position - input.WorldPos;
                float dist = length(lightVec);
                if (dist < SpotLights[j].range) {
                    float3 L = lightVec / dist;
                    float NdotL = max(dot(N, L), 0.0f);
                    float theta = dot(L, normalize(-SpotLights[j].direction));
                    float spotFactor = saturate((theta - SpotLights[j].outerCos) / max(SpotLights[j].innerCos - SpotLights[j].outerCos, 0.001f));
                    float attenuation = pow(1.0f - saturate(dist / SpotLights[j].range), 2.0);
                    float shadow = 1.0f;
                    if (SpotLights[j].shadowEnabled > 0.5f && NdotL > 0.0f && spotFactor > 0.0f) {
                        shadow = SampleSpotLightShadow(SpotLights[j], input.WorldPos);
                    }
                    float3 lightContrib = SpotLights[j].color * SpotLights[j].intensity * attenuation * spotFactor * shadow;
                    totalDiffuse += diffuseAlbedo * lightContrib * NdotL * contactDiffuseOcclusion;
                    
                    if (NdotL > 0.0f) {
                        float3 H = normalize(L + V);
                        float NdotH = max(dot(N, H), 0.0f);
                        float VdotH = max(dot(V, H), 0.0f);
                        totalSpecular += DotEvaluateSpecularBRDF(F0, roughness, NdotV, NdotL, NdotH, VdotH) *
                                         lightContrib * NdotL * contactSpecularOcclusion;
                    }
                }
            }
        }
        
        // Debug visualization mode override
        // 0=Lit, 1=Unlit, 4=LightingOnly, 6=Normals, 8=BaseColor, 9=Metallic, 10=Roughness
        if (debugVisMode == 11u) // LODVisualization
        {
            float3 lodColor = LodDebugTint;
            if (length(lodColor) < 0.001f)
                lodColor = float3(0.18f, 0.85f, 0.22f);
            return float4(saturate(lodColor), 1.0f);
        }

        float3 bakedDiffuse = diffuseAlbedo * SampleLightmap(input.UV2) * ambientOcclusion;
        float3 finalColor = bakedDiffuse + totalDiffuse + totalSpecular + emissive;
        finalColor = lerp(finalColor, finalColor * 0.35f + LodDebugTint * 0.65f, saturate(length(LodDebugTint)));
        return float4(finalColor, 1.0f);
    }
)");

uint32_t SimpleRenderer::RegisterMaterialShader(const std::string& surfaceHLSL)
{
    // Basic DJB2 hash for the HLSL string
    uint32_t hash = 5381;
    for (char c : surfaceHLSL)
        hash = ((hash << 5) + hash) + c;

    // Check if already registered
    auto it = m_CustomPSOs.find(hash);
    if (it != m_CustomPSOs.end())
        return hash;

    // Create new custom PSO
    void* pso = CreateCustomPSO(surfaceHLSL);
    if (pso)
    {
        m_CustomPSOs[hash] = {pso, surfaceHLSL};
        return hash;
    }

    return 0; // Failed
}

void* SimpleRenderer::CreateCustomPSO(const std::string& surfaceHLSL)
{
    if (!m_Initialized || !m_RootSignature)
        return nullptr;

    // Build shader source: template (with cbuffer) first, then custom code
    std::string fullCode = "#define MATERIAL_SURFACE_CUSTOM\n";
    fullCode += s_SimplePSTemplate; // Template with cbuffer, structs, forward declare
    fullCode += "\n\n// Custom surface implementation:\n";
    fullCode += surfaceHLSL; // Custom GetMaterialSurface implementation

    ComPtr<ID3DBlob> psBlob, errorBlob;
    HRESULT hr = D3DCompile(fullCode.c_str(), fullCode.length(), "CustomPS", nullptr, nullptr, "PSMain", "ps_5_0",
                            D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &psBlob, &errorBlob);

    if (FAILED(hr))
    {
        if (errorBlob)
            std::printf("Custom PSO PS compile error: %s\n", (char*)errorBlob->GetBufferPointer());
        return nullptr;
    }

    // Validate VS bytecode exists
    if (m_VSBytecode.empty())
    {
        std::printf("Custom PSO error: m_VSBytecode is empty! Initialize() may not have been called.\n");
        return nullptr;
    }

    ID3D12Device* d3dDevice = GetD3D12DevicePtr(m_Device);

    // Input layout matching standard mesh format
    D3D12_INPUT_ELEMENT_DESC inputLayout[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 40, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 1, DXGI_FORMAT_R32G32_FLOAT, 0, 48, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"TANGENT", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 56, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    };

    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.pRootSignature = reinterpret_cast<ID3D12RootSignature*>(m_RootSignature);
    psoDesc.VS = {m_VSBytecode.data(), m_VSBytecode.size()};
    psoDesc.PS = {psBlob->GetBufferPointer(), psBlob->GetBufferSize()};
    psoDesc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    psoDesc.SampleMask = UINT_MAX;
    psoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_BACK;
    psoDesc.RasterizerState.FrontCounterClockwise = FALSE;
    psoDesc.RasterizerState.DepthClipEnable = TRUE;
    psoDesc.DepthStencilState.DepthEnable = TRUE;
    psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    psoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
    psoDesc.InputLayout = {inputLayout, _countof(inputLayout)};
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets = 1;
    psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    psoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
    psoDesc.SampleDesc.Count = 1;

    ID3D12PipelineState* pso = nullptr;
    hr = d3dDevice->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&pso));
    if (FAILED(hr))
    {
        std::printf("Custom PSO creation failed: 0x%08X\n", hr);
        std::printf("  VS size: %zu, PS size: %zu\n", m_VSBytecode.size(), psBlob->GetBufferSize());
        std::printf("  RootSig: %p, Device: %p\n", (void*)psoDesc.pRootSignature, (void*)d3dDevice);

        // Dump shader source for debugging
        std::ofstream shaderDump("FailedShader.hlsl");
        if (shaderDump.is_open())
        {
            shaderDump << fullCode;
            shaderDump.close();
            std::printf("  Shader dumped to FailedShader.hlsl\n");
        }
        return nullptr;
    }

    std::printf("Custom procedural PSO created successfully\n");
    return pso;
}

SimpleRenderer::SimpleRenderer()
{
    // Initialize standard icon data
    m_LightData.numPointLights = 0;
    m_LightData.numSpotLights = 0;
}

SimpleRenderer::~SimpleRenderer()
{
    Shutdown();
}

void SimpleRenderer::SetReflectionProbeData(uint32_t slot, const std::string& cubemapPath, const Vec3& position,
                                            float radius, const Vec3& boxExtents, float intensity, float falloff,
                                            const Vec3& tint, float rotationDegrees, float blendWeight, bool enabled)
{
    if (slot >= kMaxReflectionProbeBlendCount)
        return;

    m_ReflectionProbeData[slot].cubemapPath = cubemapPath;
    m_ReflectionProbeData[slot].position = position;
    m_ReflectionProbeData[slot].radius = std::max(radius, 0.0f);
    m_ReflectionProbeData[slot].boxExtents = ResolveReflectionProbeBoxExtents(boxExtents, radius);
    m_ReflectionProbeData[slot].intensity = std::max(intensity, 0.0f);
    m_ReflectionProbeData[slot].falloff = std::clamp(falloff, 0.0f, 1.0f);
    m_ReflectionProbeData[slot].tint = tint;
    m_ReflectionProbeData[slot].rotation = rotationDegrees;
    m_ReflectionProbeData[slot].blendWeight = std::max(blendWeight, 0.0f);
    m_ReflectionProbeData[slot].enabled =
        enabled && !cubemapPath.empty() && radius > 0.0f && m_ReflectionProbeData[slot].blendWeight > 0.0f;
}

void SimpleRenderer::BeginFrame()
{
    // Reset ring buffer offset at start of frame
    m_CurrentCBOffset = 0;
    ++m_FrameCounter;
    m_SharedDescriptorFrame = 0;
    m_ReflectionProbeDescriptorSignatures.clear();
    for (uint32_t i = 0; i < kMaxReflectionProbeBlendCount; ++i)
        m_ReflectionProbeData[i] = {};

    // Update animation time (~60fps approximation)
    m_ElapsedTime += 0.016f;
}

bool SimpleRenderer::Initialize(RHIDevice* device)
{
    m_Device = device;
    m_D3D12Device = reinterpret_cast<D3D12Device*>(device);

    const std::string shaderRoot =
        ResolveShaderRootPath(m_ShaderRootPathOverride, m_AllowEditorShaderFallback);
    if (shaderRoot.empty())
    {
        DOT_LOG_ERROR("SimpleRenderer: Failed to resolve shader root.");
        return false;
    }

    ShaderCompiler::Get().Initialize(shaderRoot);

    if (!CreateShaders())
        return false;
    if (!CreatePipelineState())
        return false;
    if (!CreateAllMeshes())
        return false;
    if (!CreateConstantBuffer())
        return false;
    if (!CreateShadowResources())
        return false;

    m_Initialized = true;
    std::printf("SimpleRenderer initialized with all primitives and shadow mapping!\n");
    return true;
}

void SimpleRenderer::Shutdown()
{
    m_DirectionalShadowGraphTexture.reset();
    m_LocalShadowGraphTexture.reset();
    m_SSAOOcclusionGraphTexture.reset();
    m_SSAOBlurredGraphTexture.reset();
    m_HZBGraphTexture.reset();

    if (m_PipelineState)
    {
        reinterpret_cast<ID3D12PipelineState*>(m_PipelineState)->Release();
        m_PipelineState = nullptr;
    }
    if (m_InstancedPipelineState)
    {
        reinterpret_cast<ID3D12PipelineState*>(m_InstancedPipelineState)->Release();
        m_InstancedPipelineState = nullptr;
    }
    if (m_RootSignature)
    {
        reinterpret_cast<ID3D12RootSignature*>(m_RootSignature)->Release();
        m_RootSignature = nullptr;
    }
    if (m_CBVHeap)
    {
        reinterpret_cast<ID3D12DescriptorHeap*>(m_CBVHeap)->Release();
        m_CBVHeap = nullptr;
    }

    // Shadow resources cleanup
    if (m_ShadowDepthBuffer)
    {
        reinterpret_cast<ID3D12Resource*>(m_ShadowDepthBuffer)->Release();
        m_ShadowDepthBuffer = nullptr;
    }
    if (m_ShadowDSVHeap)
    {
        reinterpret_cast<ID3D12DescriptorHeap*>(m_ShadowDSVHeap)->Release();
        m_ShadowDSVHeap = nullptr;
    }
    if (m_ShadowSRVHeap)
    {
        reinterpret_cast<ID3D12DescriptorHeap*>(m_ShadowSRVHeap)->Release();
        m_ShadowSRVHeap = nullptr;
    }
    if (m_LocalShadowDepthBuffer)
    {
        reinterpret_cast<ID3D12Resource*>(m_LocalShadowDepthBuffer)->Release();
        m_LocalShadowDepthBuffer = nullptr;
    }
    if (m_LocalShadowDSVHeap)
    {
        reinterpret_cast<ID3D12DescriptorHeap*>(m_LocalShadowDSVHeap)->Release();
        m_LocalShadowDSVHeap = nullptr;
    }
    if (m_ShadowPSO)
    {
        reinterpret_cast<ID3D12PipelineState*>(m_ShadowPSO)->Release();
        m_ShadowPSO = nullptr;
    }
    if (m_ShadowRootSignature)
    {
        reinterpret_cast<ID3D12RootSignature*>(m_ShadowRootSignature)->Release();
        m_ShadowRootSignature = nullptr;
    }

    // HZB / occlusion resources cleanup
    if (m_Occlusion.hzbTexture)
    {
        reinterpret_cast<ID3D12Resource*>(m_Occlusion.hzbTexture)->Release();
        m_Occlusion.hzbTexture = nullptr;
    }
    if (m_Occlusion.hzbSrvHeap)
    {
        reinterpret_cast<ID3D12DescriptorHeap*>(m_Occlusion.hzbSrvHeap)->Release();
        m_Occlusion.hzbSrvHeap = nullptr;
    }
    if (m_Occlusion.hzbUavHeap)
    {
        reinterpret_cast<ID3D12DescriptorHeap*>(m_Occlusion.hzbUavHeap)->Release();
        m_Occlusion.hzbUavHeap = nullptr;
    }
    if (m_Occlusion.hzbReadbackBuffer)
    {
        reinterpret_cast<ID3D12Resource*>(m_Occlusion.hzbReadbackBuffer)->Release();
        m_Occlusion.hzbReadbackBuffer = nullptr;
    }
    if (m_Occlusion.hzbDownsamplePSO)
    {
        reinterpret_cast<ID3D12PipelineState*>(m_Occlusion.hzbDownsamplePSO)->Release();
        m_Occlusion.hzbDownsamplePSO = nullptr;
    }
    if (m_Occlusion.hzbRootSignature)
    {
        reinterpret_cast<ID3D12RootSignature*>(m_Occlusion.hzbRootSignature)->Release();
        m_Occlusion.hzbRootSignature = nullptr;
    }
    m_Occlusion.mipReadback.clear();
    m_Occlusion.readbackData.clear();
    m_Occlusion.readbackValid = false;

    // Clear all primitive meshes
    for (auto& mesh : m_Meshes)
    {
        mesh.vertexBuffer.reset();
        mesh.indexBuffer.reset();
        mesh.indexCount = 0;
    }

    // Clear texture cache and release SRV heaps
    {
        // std::lock_guard<std::mutex> lock(m_TextureCacheMutex);
        for (auto& pair : m_TextureCache)
        {
            if (pair.second.srvHeap)
            {
                reinterpret_cast<ID3D12DescriptorHeap*>(pair.second.srvHeap)->Release();
            }
        }
        m_TextureCache.clear();
    }

    if (m_SamplerHeap)
    {
        reinterpret_cast<ID3D12DescriptorHeap*>(m_SamplerHeap)->Release();
        m_SamplerHeap = nullptr;
    }

    m_LightConstantBuffer.reset();
    m_MappedLightBuffer = nullptr;
    if (m_InstanceCBResource)
    {
        reinterpret_cast<ID3D12Resource*>(m_InstanceCBResource)->Release();
        m_InstanceCBResource = nullptr;
    }
    m_MappedInstanceBuffer = nullptr;
    m_Initialized = false;
}

bool SimpleRenderer::CreateShaders()
{
    // Vertex shader with MVP transform, lighting support, and UV coordinates
    const char* vsCode = R"(
        cbuffer ConstantBuffer : register(b0) { 
            float4x4 MVP;
            float4x4 Model;
        };
        struct VSInput { 
            float3 Position : POSITION; 
            float3 Normal : NORMAL;
            float4 Tangent : TANGENT;
            float4 Color : COLOR;
            float2 UV : TEXCOORD0;
            float2 UV2 : TEXCOORD1;
        };
        struct VSOutput { 
            float4 Position : SV_POSITION; 
            float3 WorldPos : TEXCOORD0;
            float3 WorldNormal : NORMAL;
            float4 WorldTangent : TANGENT;
            float4 Color : COLOR;
            float2 UV : TEXCOORD1;
            float2 UV2 : TEXCOORD2;
        };
        VSOutput VSMain(VSInput input) {
            VSOutput output;
            output.Position = mul(MVP, float4(input.Position, 1.0f));
            float4 worldPos = mul(Model, float4(input.Position, 1.0f));
            output.WorldPos = worldPos.xyz;
            output.WorldNormal = normalize(mul((float3x3)Model, input.Normal));
            float3 worldTangent = mul((float3x3)Model, input.Tangent.xyz);
            worldTangent = worldTangent - output.WorldNormal * dot(output.WorldNormal, worldTangent);
            float tangentLenSq = dot(worldTangent, worldTangent);
            if (tangentLenSq > 1e-8f)
                worldTangent *= rsqrt(tangentLenSq);
            output.WorldTangent = float4(worldTangent, input.Tangent.w);
            output.Color = input.Color;
            output.UV = input.UV;
            output.UV2 = input.UV2;
            return output;
        }
    )";

    std::string psCode = std::string(R"(
        #define MAX_POINT_LIGHTS 16
        #define MAX_SPOT_LIGHTS 16
        
        // Texture and sampler
        Texture2D AlbedoTex : register(t0);
        Texture2D tex1 : register(t1);
        Texture2D tex2 : register(t2);
        Texture2D tex3 : register(t3);
        SamplerState AlbedoSampler : register(s0);
        #define tex0 AlbedoTex
        #define sampler0 AlbedoSampler
        #define sampler1 AlbedoSampler
        #define sampler2 AlbedoSampler
        #define sampler3 AlbedoSampler
        
        // Shadow mapping
        Texture2D ShadowMap : register(t4);
        Texture2D SSAOTex : register(t5);
        Texture2DArray LocalShadowMap : register(t6);
        SamplerComparisonState ShadowSampler : register(s4);
        SamplerState ShadowSamplerPoint : register(s5); // For blocker search (non-comparison)
        SamplerState SSAOSampler : register(s7);
)") + s_SharedLightingBufferHLSL + s_NormalMapHelpersTemplateHLSL + R"(
        struct PSInput { 
            float4 Position : SV_POSITION; 
            float3 WorldPos : TEXCOORD0;
            float3 WorldNormal : NORMAL;
            float4 WorldTangent : TANGENT;
            float4 Color : COLOR;
            float2 UV : TEXCOORD1;
            float2 UV2 : TEXCOORD2;
        };
        
        float CalcSpecular(float3 N, float3 L, float3 V, float roughness) {
            float3 H = normalize(L + V);
            float NdotH = max(dot(N, H), 0.0f);
            float shininess = max(2.0f, (1.0f - roughness) * 128.0f);
            return pow(NdotH, shininess);
        }
        
        float SampleShadow(float3 worldPos) {
            float4 shadowCoord = mul(ShadowMatrix, float4(worldPos, 1.0f));
            shadowCoord.xyz /= shadowCoord.w;
            shadowCoord.xy = shadowCoord.xy * 0.5f + 0.5f;
            shadowCoord.y = 1.0f - shadowCoord.y;
            if (shadowCoord.x < 0 || shadowCoord.x > 1 || shadowCoord.y < 0 || shadowCoord.y > 1 || shadowCoord.z < 0 || shadowCoord.z > 1)
                return 2.0; // OOB marker
            float depth = shadowCoord.z - ShadowBias;
            return ShadowMap.SampleCmpLevelZero(ShadowSampler, shadowCoord.xy, depth);
        }

        void GetPointLightFaceBasis(int faceIndex, out float3 forward, out float3 up)
        {
            if (faceIndex == 0) { forward = float3(1, 0, 0); up = float3(0, 1, 0); return; }
            if (faceIndex == 1) { forward = float3(-1, 0, 0); up = float3(0, 1, 0); return; }
            if (faceIndex == 2) { forward = float3(0, 1, 0); up = float3(0, 0, -1); return; }
            if (faceIndex == 3) { forward = float3(0, -1, 0); up = float3(0, 0, 1); return; }
            if (faceIndex == 4) { forward = float3(0, 0, 1); up = float3(0, 1, 0); return; }
            forward = float3(0, 0, -1); up = float3(0, 1, 0);
        }

        int SelectPointLightShadowFace(float3 lightToPoint)
        {
            float3 absVec = abs(lightToPoint);
            if (absVec.x >= absVec.y && absVec.x >= absVec.z)
                return lightToPoint.x >= 0.0f ? 0 : 1;
            if (absVec.y >= absVec.x && absVec.y >= absVec.z)
                return lightToPoint.y >= 0.0f ? 2 : 3;
            return lightToPoint.z >= 0.0f ? 4 : 5;
        }

        static const float2 kLocalShadowPoissonDisk[12] = {
            float2(-0.326f, -0.406f), float2(-0.840f, -0.074f), float2(-0.696f,  0.457f), float2(-0.203f,  0.621f),
            float2( 0.962f, -0.195f), float2( 0.473f, -0.480f), float2( 0.519f,  0.767f), float2( 0.185f, -0.893f),
            float2( 0.507f,  0.064f), float2( 0.896f,  0.412f), float2(-0.322f, -0.933f), float2(-0.792f, -0.598f)
        };

        float SampleLocalShadowPCF(float2 uv, float slice, float compareDepth, float filterRadius)
        {
            const float texel = 1.0f / 1024.0f;
            float visibility = 0.0f;
            [unroll]
            for (int i = 0; i < 12; ++i)
            {
                float2 sampleUV = uv + kLocalShadowPoissonDisk[i] * texel * filterRadius;
                visibility += LocalShadowMap.SampleCmpLevelZero(ShadowSampler, float3(sampleUV, slice), compareDepth);
            }

            visibility /= 12.0f;
            visibility = saturate((visibility - 0.06f) / 0.94f);
            return visibility * visibility;
        }

        float SamplePointLightShadow(PointLight light, float3 worldPos)
        {
            if (light.shadowEnabled < 0.5f)
                return 1.0f;

            float3 lightToPoint = worldPos - light.position;
            int faceIndex = SelectPointLightShadowFace(lightToPoint);
            float3 forward, up;
            GetPointLightFaceBasis(faceIndex, forward, up);
            float3 right = normalize(cross(up, forward));
            float3 localUp = cross(forward, right);

            float faceZ = dot(lightToPoint, forward);
            if (faceZ <= 0.0f)
                return 1.0f;

            float faceX = dot(lightToPoint, right);
            float faceY = dot(lightToPoint, localUp);
            float2 uv = float2(faceX / faceZ, -faceY / faceZ) * 0.5f + 0.5f;
            if (uv.x < 0.0f || uv.x > 1.0f || uv.y < 0.0f || uv.y > 1.0f)
                return 1.0f;

            const float nearPlane = 0.05f;
            const float farPlane = max(light.range, nearPlane + 0.001f);
            float compareDepth = (farPlane / (farPlane - nearPlane)) - ((nearPlane * farPlane) / ((farPlane - nearPlane) * faceZ));
            compareDepth -= light.shadowBias;
            float filterRadius = lerp(1.25f, 3.25f, saturate(faceZ / farPlane));
            return SampleLocalShadowPCF(uv, light.shadowBaseSlice + faceIndex, compareDepth, filterRadius);
        }

        float SampleSpotLightShadow(SpotLight light, float3 worldPos) {
            if (light.shadowEnabled < 0.5f) return 1.0f;
            float3 f = normalize(light.direction), p = worldPos - light.position;
            float z = dot(p, f); if (z <= 0.0f) return 1.0f;
            float3 refUp = abs(f.y) > 0.99f ? float3(0,0,1) : float3(0,1,0);
            float3 r = normalize(cross(refUp, f)), u = cross(f, r);
            float t = tan(max(acos(saturate(light.outerCos)), 0.0174533f));
            float2 uv = float2(dot(p, r) / (z * t), -dot(p, u) / (z * t)) * 0.5f + 0.5f;
            if (uv.x < 0.0f || uv.x > 1.0f || uv.y < 0.0f || uv.y > 1.0f) return 1.0f;
            const float n = 0.05f, fa = max(light.range, n + 0.001f);
            float d = (fa / (fa - n)) - ((n * fa) / ((fa - n) * z)) - light.shadowBias;
            float filterRadius = lerp(1.0f, 2.75f, saturate(z / fa));
            return SampleLocalShadowPCF(uv, light.shadowBaseSlice, d, filterRadius);
        }

        float3 SampleLightmap(float2 uv)
        {
            if (LightmapEnabled < 0.5f || LightmapTextureSlot < 0.5f)
                return float3(0.0f, 0.0f, 0.0f);

            const float2 lightmapUv = uv * LightmapUVScale + LightmapUVOffset;
            if (LightmapTextureSlot < 1.5f)
                return tex1.Sample(AlbedoSampler, lightmapUv).rgb * LightmapIntensity;
            if (LightmapTextureSlot < 2.5f)
                return tex2.Sample(AlbedoSampler, lightmapUv).rgb * LightmapIntensity;
            return tex3.Sample(AlbedoSampler, lightmapUv).rgb * LightmapIntensity;
        }
        
        float4 PSMain(PSInput input) : SV_TARGET { 
            float3 N = normalize(input.WorldNormal);
            float3 V = normalize(CameraPos - input.WorldPos);
            float metallic = MaterialMetallic;
            float roughness = MaterialRoughness;
            float ao = MaterialAmbientOcclusion;
            float2 panSpeed = PannerSpeed;
            if (PannerLink > 0) panSpeed.y = panSpeed.x;
            float2 uv = input.UV * UVTiling + UVOffset;
            float2 texUV = uv;
            if (PannerMethod == 0)
                texUV = uv + _Time * panSpeed;
            else if (PannerMethod == 1)
                texUV = uv + panSpeed * sin(_Time);
            else if (PannerMethod == 2)
                texUV = uv + panSpeed * (abs(frac(_Time * 0.5f) * 2.0f - 1.0f) * 2.0f - 1.0f);
            else if (PannerMethod == 3)
            {
                float s, c; sincos(_Time * panSpeed.x, s, c);
                texUV = mul(uv - 0.5f, float2x2(c, -s, s, c)) + 0.5f;
            }

            int normalSlot = (int)(NormalTextureSlot + 0.5f);
            if (NormalTextureSlot > -0.5f && normalSlot >= 0 && normalSlot < 4 && HasTextures[normalSlot] > 0.5f)
            N = DotDecodeNormalSample(SampleMaterialTexture(normalSlot, texUV).xyz, texUV, input.WorldPos, input.WorldNormal, input.WorldTangent);

            int ormSlot = (int)(OrmTextureSlot + 0.5f);
            if (OrmTextureSlot > -0.5f && ormSlot >= 0 && ormSlot < 4 && HasTextures[ormSlot] > 0.5f)
            {
                float3 orm = SampleMaterialTexture(ormSlot, texUV).rgb;
                ao = saturate(orm.r);
                roughness = saturate(orm.g);
                metallic = saturate(orm.b);
            }

            float3 baseColor = MaterialColor;
            int albedoSlot = (int)(AlbedoTextureSlot + 0.5f);
            if (AlbedoTextureSlot > -0.5f && albedoSlot >= 0 && albedoSlot < 4 && HasTextures[albedoSlot] > 0.5f)
                baseColor = SampleMaterialTexture(albedoSlot, texUV).rgb;

            metallic = saturate(metallic);
            roughness = clamp(roughness, 0.045f, 1.0f);
            baseColor = saturate(baseColor);

            if (debugVisMode == 1u) // Unlit
                return float4(baseColor, 1.0f);
            if (debugVisMode == 6u) // Normals
                return float4(N * 0.5f + 0.5f, 1.0f);
            if (debugVisMode == 8u) // BaseColor
                return float4(baseColor, 1.0f);
            if (debugVisMode == 9u) // Metallic
                return float4(metallic, metallic, metallic, 1.0f);
            if (debugVisMode == 10u) // Roughness
                return float4(roughness, roughness, roughness, 1.0f);

            // Sample SSAO from screen-space buffer (t5) only when the pass is available.
            float ssao = 1.0f;
            if (SSAOEnabled > 0.5f && ViewportWidth > 0.0f && ViewportHeight > 0.0f)
            {
                float2 screenUV = input.Position.xy / float2(ViewportWidth, ViewportHeight);
                ssao = saturate(SSAOTex.Sample(SSAOSampler, screenUV).r);
            }
            
            float3 macroN = normalize(input.WorldNormal);
            float aoFactor = lerp(1.0f, saturate(ao), 0.90f);
            float ambientOcclusion = max(lerp(1.0f, ssao, 0.85f), 0.18f) * aoFactor;
            float contactDiffuseOcclusion = lerp(1.0f, ambientOcclusion, 0.35f);
            float contactSpecularOcclusion = lerp(1.0f, ambientOcclusion, 0.20f);
            float NdotV = max(dot(N, V), 0.0f);
            float3 shadingAlbedo = debugVisMode == 4u ? float3(1.0f, 1.0f, 1.0f) : baseColor;
            float3 diffuseAlbedo = shadingAlbedo * (1.0f - metallic);
            float3 F0 = lerp(float3(0.04f, 0.04f, 0.04f), shadingAlbedo, metallic);
            float3 totalDiffuse = DotComputeSkylightDiffuse(diffuseAlbedo, N, ambientOcclusion);
            float probeCoverage = 0.0f;
            float3 probeSpecular =
                DotComputeReflectionProbeSpecular(input.WorldPos, F0, roughness, N, V, ambientOcclusion, probeCoverage);
            float3 skySpecular = DotComputeSkylightSpecular(F0, roughness, N, V, ambientOcclusion);
            float3 totalSpecular = skySpecular * (1.0f - probeCoverage) + probeSpecular;
            
            // Directional light with shadow
            if (DirLightIntensity > 0.0f) {
                float3 L = normalize(-DirLightDir);
                float rawMacroNdotL = dot(macroN, L);
                float macroLightGate = saturate((rawMacroNdotL + 0.12f) / 0.36f);
                float NdotL = max(dot(N, L), 0.0f) * macroLightGate;
                
                float shadow = 1.0f;
                if (rawMacroNdotL > 0.0f) {
                    float3 pos = input.WorldPos;
                    float4 shadowCoord = mul(ShadowMatrix, float4(pos, 1.0f));
                    shadowCoord.xyz /= shadowCoord.w;
                    shadowCoord.xy = shadowCoord.xy * 0.5f + 0.5f;
                    shadowCoord.y = 1.0f - shadowCoord.y;
                    
                    if (shadowCoord.x < 0 || shadowCoord.x > 1 || shadowCoord.y < 0 || shadowCoord.y > 1 || shadowCoord.z < 0 || shadowCoord.z > 1) {
                        shadow = 1.0f;
                    } else {
                        float receiverDepth = shadowCoord.z;
                        float2 texelSize = float2(1.0f / 2048.0f, 1.0f / 2048.0f);
                        float lightSize = 2.0f;
                        float blockerSearchRadius = 16.0f;
                        float blockerSum = 0.0f;
                        float blockerCount = 0.0f;
                        [unroll]
                        for (int bx = -2; bx <= 2; bx++) {
                            [unroll]
                            for (int by = -2; by <= 2; by++) {
                                float2 offset = float2(bx, by) * texelSize * blockerSearchRadius * 0.5f;
                                float blockerDepth = ShadowMap.SampleLevel(ShadowSamplerPoint, shadowCoord.xy + offset, 0).r;
                                if (blockerDepth < receiverDepth - ShadowBias) {
                                    blockerSum += blockerDepth;
                                    blockerCount += 1.0f;
                                }
                            }
                        }
                        if (blockerCount < 0.5f) {
                            shadow = 1.0f;
                        } else {
                            float avgBlockerDepth = blockerSum / blockerCount;
                            float penumbraWidth = lightSize * (receiverDepth - avgBlockerDepth) / max(avgBlockerDepth, 0.001f);
                            penumbraWidth = clamp(penumbraWidth * 50.0f, 1.0f, 20.0f);
                            float pcfSum = 0.0f;
                            float sampleCount = 0.0f;
                            [unroll]
                            for (int px = -3; px <= 3; px++) {
                                [unroll]
                                for (int py = -3; py <= 3; py++) {
                                    float2 offset = float2(px, py) * texelSize * penumbraWidth * 0.5f;
                                    pcfSum += ShadowMap.SampleCmpLevelZero(ShadowSampler, shadowCoord.xy + offset, receiverDepth - ShadowBias);
                                    sampleCount += 1.0f;
                                }
                            }
                            
                            shadow = pcfSum / sampleCount;
                        }
                    }
                }
                
                float shadowFactor = shadow;
                float3 radiance = DirLightColor * DirLightIntensity * shadowFactor;
                totalDiffuse += diffuseAlbedo * radiance * NdotL * contactDiffuseOcclusion;
                
                if (NdotL > 0.0f) {
                    float3 H = normalize(L + V);
                    float NdotH = max(dot(N, H), 0.0f);
                    float VdotH = max(dot(V, H), 0.0f);
                    totalSpecular += DotEvaluateSpecularBRDF(F0, roughness, NdotV, NdotL, NdotH, VdotH) * radiance * NdotL *
                                     contactSpecularOcclusion;
                }
            }
            
            for (int i = 0; i < NumPointLights && i < MAX_POINT_LIGHTS; i++) {
                float3 lightVec = PointLights[i].position - input.WorldPos;
                float dist = length(lightVec);
                if (dist < PointLights[i].range) {
                    float3 L = lightVec / dist;
                    float NdotL = max(dot(N, L), 0.0f);
                    float attenuation = 1.0f - saturate(dist / PointLights[i].range);
                    attenuation *= attenuation;
                    float shadow = 1.0f;
                    if (NdotL > 0.0f && PointLights[i].shadowEnabled > 0.5f) {
                        shadow = SamplePointLightShadow(PointLights[i], input.WorldPos);
                    }
                    
                    float3 lightContrib = PointLights[i].color * PointLights[i].intensity * attenuation * shadow;
                    totalDiffuse += diffuseAlbedo * lightContrib * NdotL * contactDiffuseOcclusion;
                    
                    if (NdotL > 0.0f) {
                        float3 H = normalize(L + V);
                        float NdotH = max(dot(N, H), 0.0f);
                        float VdotH = max(dot(V, H), 0.0f);
                        totalSpecular += DotEvaluateSpecularBRDF(F0, roughness, NdotV, NdotL, NdotH, VdotH) *
                                         lightContrib * NdotL * contactSpecularOcclusion;
                    }
                }
            }
            
            for (int j = 0; j < NumSpotLights && j < MAX_SPOT_LIGHTS; j++) {
                float3 lightVec = SpotLights[j].position - input.WorldPos;
                float dist = length(lightVec);
                if (dist < SpotLights[j].range) {
                    float3 L = lightVec / dist;
                    float NdotL = max(dot(N, L), 0.0f);
                    float theta = dot(L, normalize(-SpotLights[j].direction));
                    float epsilon = SpotLights[j].innerCos - SpotLights[j].outerCos;
                    float spotFactor = saturate((theta - SpotLights[j].outerCos) / max(epsilon, 0.001f));
                    float attenuation = 1.0f - saturate(dist / SpotLights[j].range);
                    attenuation *= attenuation;
                    
                    float shadow = 1.0f;
                    if (NdotL > 0.0f && spotFactor > 0.0f && SpotLights[j].shadowEnabled > 0.5f) {
                        shadow = SampleSpotLightShadow(SpotLights[j], input.WorldPos);
                    }

                    float3 lightContrib = SpotLights[j].color * SpotLights[j].intensity * attenuation * spotFactor * shadow;
                    totalDiffuse += diffuseAlbedo * lightContrib * NdotL * contactDiffuseOcclusion;
                    
                    if (NdotL > 0.0f) {
                        float3 H = normalize(L + V);
                        float NdotH = max(dot(N, H), 0.0f);
                        float VdotH = max(dot(V, H), 0.0f);
                        totalSpecular += DotEvaluateSpecularBRDF(F0, roughness, NdotV, NdotL, NdotH, VdotH) *
                                         lightContrib * NdotL * contactSpecularOcclusion;
                    }
                }
            }
            
            float3 emissive = MaterialEmissiveColor * MaterialEmissiveStrength;
            float3 bakedDiffuse = diffuseAlbedo * SampleLightmap(input.UV2) * ambientOcclusion;
            if (debugVisMode == 11u) // LODVisualization
            {
                float3 lodColor = LodDebugTint;
                if (length(lodColor) < 0.001f)
                    lodColor = float3(0.18f, 0.85f, 0.22f);
                return float4(saturate(lodColor), 1.0f);
            }
            float3 finalColor = bakedDiffuse + totalDiffuse + totalSpecular + emissive;
            finalColor = lerp(finalColor, finalColor * 0.35f + LodDebugTint * 0.65f, saturate(length(LodDebugTint)));
            return float4(finalColor, 1.0f);
        }
    )";

    ComPtr<ID3DBlob> vsBlob, psBlob, errorBlob;

    HRESULT hr = D3DCompile(vsCode, strlen(vsCode), "VS", nullptr, nullptr, "VSMain", "vs_5_0",
                            D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &vsBlob, &errorBlob);
    if (FAILED(hr))
    {
        if (errorBlob)
            std::printf("VS compile error: %s\n", (char*)errorBlob->GetBufferPointer());
        return false;
    }

    // Use the original psCode (s_SimplePSTemplate caused rendering issues)
    hr = D3DCompile(psCode.c_str(), psCode.length(), "PS", nullptr, nullptr, "PSMain", "ps_5_0",
                    D3DCOMPILE_OPTIMIZATION_LEVEL3,
                    0, &psBlob, &errorBlob);
    if (FAILED(hr))
    {
        if (errorBlob)
            std::printf("PS compile error: %s\n", (char*)errorBlob->GetBufferPointer());
        return false;
    }

    m_VSBytecode.resize(vsBlob->GetBufferSize());
    memcpy(m_VSBytecode.data(), vsBlob->GetBufferPointer(), vsBlob->GetBufferSize());

    // Instanced vertex shader: per-instance world matrix is read from b1 using SV_InstanceID.
    const char* vsInstancedCode = R"(
        cbuffer ConstantBuffer : register(b0) {
            float4x4 ViewProjection;
            float4x4 _UnusedModel;
        };
        cbuffer InstanceBuffer : register(b1) {
            float4x4 InstanceModels[64];
        };

        struct VSInput {
            float3 Position : POSITION;
            float3 Normal : NORMAL;
            float4 Tangent : TANGENT;
            float4 Color : COLOR;
            float2 UV : TEXCOORD0;
            float2 UV2 : TEXCOORD1;
        };

        struct VSOutput {
            float4 Position : SV_POSITION;
            float3 WorldPos : TEXCOORD0;
            float3 WorldNormal : NORMAL;
            float4 WorldTangent : TANGENT;
            float4 Color : COLOR;
            float2 UV : TEXCOORD1;
            float2 UV2 : TEXCOORD2;
        };

        VSOutput VSMain(VSInput input, uint instanceId : SV_InstanceID)
        {
            VSOutput output;
            float4x4 model = InstanceModels[instanceId];

            float4 localPos = float4(input.Position, 1.0f);
            float4 worldPos = mul(model, localPos);
            output.Position = mul(ViewProjection, worldPos);
            output.WorldPos = worldPos.xyz;
            output.WorldNormal = normalize(mul((float3x3)model, input.Normal));
            float3 worldTangent = mul((float3x3)model, input.Tangent.xyz);
            worldTangent = worldTangent - output.WorldNormal * dot(output.WorldNormal, worldTangent);
            float tangentLenSq = dot(worldTangent, worldTangent);
            if (tangentLenSq > 1e-8f)
                worldTangent *= rsqrt(tangentLenSq);
            output.WorldTangent = float4(worldTangent, input.Tangent.w);
            output.Color = input.Color;
            output.UV = input.UV;
            output.UV2 = input.UV2;
            return output;
        }
    )";

    ComPtr<ID3DBlob> vsInstancedBlob;
    hr = D3DCompile(vsInstancedCode, strlen(vsInstancedCode), "VS_INSTANCED", nullptr, nullptr, "VSMain", "vs_5_0",
                    D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &vsInstancedBlob, &errorBlob);
    if (FAILED(hr))
    {
        if (errorBlob)
            std::printf("Instanced VS compile error: %s\n", (char*)errorBlob->GetBufferPointer());
        return false;
    }

    m_InstancedVSBytecode.resize(vsInstancedBlob->GetBufferSize());
    memcpy(m_InstancedVSBytecode.data(), vsInstancedBlob->GetBufferPointer(), vsInstancedBlob->GetBufferSize());

    m_PSBytecode.resize(psBlob->GetBufferSize());
    memcpy(m_PSBytecode.data(), psBlob->GetBufferPointer(), psBlob->GetBufferSize());

    // Compile depth visualization pixel shader
    // Uses SV_POSITION.z (depth in [0,1]) and outputs as grayscale
    const char* depthPsCode = R"(
        struct PSInput {
            float4 Position : SV_POSITION;
            float3 WorldPos : TEXCOORD0;
            float3 WorldNormal : NORMAL;
            float4 WorldTangent : TANGENT;
            float4 Color : COLOR;
            float2 UV : TEXCOORD1;
            float2 UV2 : TEXCOORD2;
        };
        float4 PSMain(PSInput input) : SV_TARGET {
            // SV_POSITION.z is already in [0,1] after perspective divide
            float depth = input.Position.z;
            // Apply contrast curve for better visualization (closer = darker)
            depth = pow(depth, 0.3);
            return float4(depth, depth, depth, 1.0);
        }
    )";

    ComPtr<ID3DBlob> depthPsBlob;
    hr = D3DCompile(depthPsCode, strlen(depthPsCode), "DepthPS", nullptr, nullptr, "PSMain", "ps_5_0",
                    D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &depthPsBlob, &errorBlob);
    if (FAILED(hr))
    {
        if (errorBlob)
            std::printf("Depth PS compile error: %s\n", (char*)errorBlob->GetBufferPointer());
        return false;
    }

    m_DepthPSBytecode.resize(depthPsBlob->GetBufferSize());
    memcpy(m_DepthPSBytecode.data(), depthPsBlob->GetBufferPointer(), depthPsBlob->GetBufferSize());

    // Compile skybox vertex shader
    // Transforms cube vertices and passes direction to pixel shader
    const char* skyboxVsCode = R"(
        cbuffer SkyboxCB : register(b0) {
            float4x4 ViewProjection;
            float3 CameraPos;
            float _pad0;
            float3 TintColor;
            float _pad1;
            float4 RotationParams;
        };
        
        struct VSInput {
            float3 Position : POSITION;
            float3 Normal : NORMAL;
            float4 Color : COLOR;
        };
        
        struct VSOutput {
            float4 Position : SV_POSITION;
            float3 TexCoord : TEXCOORD0;  // Direction for cubemap sampling
        };
        
        VSOutput VSMain(VSInput input) {
            VSOutput output;
            float3 rotatedDir = float3(
                input.Position.x * RotationParams.x + input.Position.z * RotationParams.y,
                input.Position.y,
                -input.Position.x * RotationParams.y + input.Position.z * RotationParams.x);
            // Position skybox at camera, scale large
            float3 worldPos = input.Position * 1000.0f + CameraPos;
            output.Position = mul(ViewProjection, float4(worldPos, 1.0f));
            // Make sure skybox is at far plane (z = w for depth = 1)
            output.Position.z = output.Position.w * 0.9999f;
            // Keep the cubemap sample direction aligned with the rotated skybox.
            output.TexCoord = rotatedDir;
            return output;
        }
    )";

    // Compile skybox pixel shader
    // Samples cubemap texture using view direction
    const char* skyboxPsCode = R"(
        TextureCube SkyboxTexture : register(t0);
        SamplerState SkyboxSampler : register(s0);
        
        cbuffer SkyboxCB : register(b0) {
            float4x4 ViewProjection;
            float3 CameraPos;
            float _pad0;
            float3 TintColor;
            float _pad1;
            float4 RotationParams;
        };
        
        struct PSInput {
            float4 Position : SV_POSITION;
            float3 TexCoord : TEXCOORD0;
        };
        
        float4 PSMain(PSInput input) : SV_TARGET {
            // Sample cubemap using direction from vertex shader
            float3 color = SkyboxTexture.Sample(SkyboxSampler, normalize(input.TexCoord)).rgb;
            // Apply tint color
            return float4(color * TintColor, 1.0f);
        }
    )";

    ComPtr<ID3DBlob> skyboxVsBlob, skyboxPsBlob;
    hr = D3DCompile(skyboxVsCode, strlen(skyboxVsCode), "SkyboxVS", nullptr, nullptr, "VSMain", "vs_5_0",
                    D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &skyboxVsBlob, &errorBlob);
    if (FAILED(hr))
    {
        if (errorBlob)
            std::printf("Skybox VS compile error: %s\n", (char*)errorBlob->GetBufferPointer());
        return false;
    }

    hr = D3DCompile(skyboxPsCode, strlen(skyboxPsCode), "SkyboxPS", nullptr, nullptr, "PSMain", "ps_5_0",
                    D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &skyboxPsBlob, &errorBlob);
    if (FAILED(hr))
    {
        if (errorBlob)
            std::printf("Skybox PS compile error: %s\n", (char*)errorBlob->GetBufferPointer());
        return false;
    }

    m_SkyboxVSBytecode.resize(skyboxVsBlob->GetBufferSize());
    memcpy(m_SkyboxVSBytecode.data(), skyboxVsBlob->GetBufferPointer(), skyboxVsBlob->GetBufferSize());

    m_SkyboxPSBytecode.resize(skyboxPsBlob->GetBufferSize());
    memcpy(m_SkyboxPSBytecode.data(), skyboxPsBlob->GetBufferPointer(), skyboxPsBlob->GetBufferSize());

    return true;
}

bool SimpleRenderer::CreatePipelineState()
{
    ID3D12Device* d3dDevice = GetD3D12DevicePtr(m_Device);

    // Create CBV descriptor heap
    D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
    heapDesc.NumDescriptors = 1;
    heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    ID3D12DescriptorHeap* cbvHeap = nullptr;
    HRESULT hr = d3dDevice->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&cbvHeap));
    if (FAILED(hr))
        return false;
    m_CBVHeap = cbvHeap;

    // Create constant buffer for light data
    // Size must be 256-byte aligned
    // Create constant buffer for light data
    // Size is now kMaxCBSize for dynamic ring buffer usage
    const UINT cbSize = kMaxCBSize;

    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;

    D3D12_RESOURCE_DESC bufferDesc = {};
    bufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    bufferDesc.Width = cbSize;
    bufferDesc.Height = 1;
    bufferDesc.DepthOrArraySize = 1;
    bufferDesc.MipLevels = 1;
    bufferDesc.SampleDesc.Count = 1;
    bufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    ID3D12Resource* cbResource = nullptr;
    hr = d3dDevice->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &bufferDesc,
                                            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&cbResource));
    if (FAILED(hr))
        return false;

    // Map buffer persistently
    void* mappedData = nullptr;
    D3D12_RANGE readRange = {0, 0};
    cbResource->Map(0, &readRange, &mappedData);
    m_MappedLightBuffer = mappedData;

    // Store as raw pointer (we manage lifetime manually)
    m_LightConstantBuffer = nullptr; // Will use cbResource directly

    // Store resource for access in Render and cleanup
    m_CBResource = cbResource;

    // Dedicated instance matrix constant buffer (b1)
    // Holds up to kMaxInstanceBatch world matrices.
    D3D12_RESOURCE_DESC instanceCbDesc = bufferDesc;
    const UINT instanceCbSize = ((kMaxInstanceBatch * sizeof(float) * 16) + 255) & ~255u;
    instanceCbDesc.Width = instanceCbSize;

    ID3D12Resource* instanceCbResource = nullptr;
    hr = d3dDevice->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &instanceCbDesc,
                                            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                            IID_PPV_ARGS(&instanceCbResource));
    if (FAILED(hr))
        return false;

    void* mappedInstanceData = nullptr;
    hr = instanceCbResource->Map(0, &readRange, &mappedInstanceData);
    if (FAILED(hr))
    {
        instanceCbResource->Release();
        return false;
    }

    m_InstanceCBResource = instanceCbResource;
    m_MappedInstanceBuffer = mappedInstanceData;

    // Root signature with CBV, SRV table, and Sampler table for texture support
    // SRV range: t0-t4 (material textures + shadow map)
    D3D12_DESCRIPTOR_RANGE materialSrvRange = {};
    materialSrvRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    materialSrvRange.NumDescriptors = 4;     // t0-t3 for material textures (shadow at t4 uses separate param)
    materialSrvRange.BaseShaderRegister = 0; // t0
    materialSrvRange.RegisterSpace = 0;
    materialSrvRange.OffsetInDescriptorsFromTableStart = 0;

    // Sampler range: s0-s4 (material samplers + shadow comparison sampler)
    D3D12_DESCRIPTOR_RANGE materialSamplerRange = {};
    materialSamplerRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
    materialSamplerRange.NumDescriptors = 4;     // s0-s3 for material samplers (s4 is static shadow sampler)
    materialSamplerRange.BaseShaderRegister = 0; // s0
    materialSamplerRange.RegisterSpace = 0;
    materialSamplerRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_ROOT_PARAMETER rootParams[6] = {};

    // Root param 0: CBV
    rootParams[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParams[0].Descriptor.ShaderRegister = 0;
    rootParams[0].Descriptor.RegisterSpace = 0;
    rootParams[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    // Root param 1: SRV table for material textures (t0-t3)
    rootParams[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParams[1].DescriptorTable.NumDescriptorRanges = 1;
    rootParams[1].DescriptorTable.pDescriptorRanges = &materialSrvRange;
    rootParams[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    // Root param 2: Sampler table for material samplers (s0-s3)
    rootParams[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParams[2].DescriptorTable.NumDescriptorRanges = 1;
    rootParams[2].DescriptorTable.pDescriptorRanges = &materialSamplerRange;
    rootParams[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    // Root param 3: directional shadow, SSAO, and local shadow SRV table (t4-t6)
    D3D12_DESCRIPTOR_RANGE shadowSrvRange = {};
    shadowSrvRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    shadowSrvRange.NumDescriptors = 3;     // t4=ShadowMap, t5=SSAOTex, t6=LocalShadowMap
    shadowSrvRange.BaseShaderRegister = 4; // t4
    shadowSrvRange.RegisterSpace = 0;
    shadowSrvRange.OffsetInDescriptorsFromTableStart = 0;

    rootParams[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParams[3].DescriptorTable.NumDescriptorRanges = 1;
    rootParams[3].DescriptorTable.pDescriptorRanges = &shadowSrvRange;
    rootParams[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    // Static samplers for shadow comparison and reflection probe sampling
    D3D12_STATIC_SAMPLER_DESC staticSamplers[4] = {};

    // s4: Comparison sampler for PCF
    staticSamplers[0].Filter = D3D12_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT;
    staticSamplers[0].AddressU = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
    staticSamplers[0].AddressV = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
    staticSamplers[0].AddressW = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
    staticSamplers[0].MipLODBias = 0;
    staticSamplers[0].MaxAnisotropy = 1;
    staticSamplers[0].ComparisonFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
    staticSamplers[0].BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE;
    staticSamplers[0].MinLOD = 0;
    staticSamplers[0].MaxLOD = D3D12_FLOAT32_MAX;
    staticSamplers[0].ShaderRegister = 4; // s4
    staticSamplers[0].RegisterSpace = 0;
    staticSamplers[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    // s5: Point sampler for PCSS blocker search
    staticSamplers[1].Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
    staticSamplers[1].AddressU = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
    staticSamplers[1].AddressV = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
    staticSamplers[1].AddressW = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
    staticSamplers[1].MipLODBias = 0;
    staticSamplers[1].MaxAnisotropy = 1;
    staticSamplers[1].ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
    staticSamplers[1].BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE;
    staticSamplers[1].MinLOD = 0;
    staticSamplers[1].MaxLOD = D3D12_FLOAT32_MAX;
    staticSamplers[1].ShaderRegister = 5; // s5
    staticSamplers[1].RegisterSpace = 0;
    staticSamplers[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    // s6: Linear clamp sampler for reflection probes
    staticSamplers[2].Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    staticSamplers[2].AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    staticSamplers[2].AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    staticSamplers[2].AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    staticSamplers[2].MipLODBias = 0;
    staticSamplers[2].MaxAnisotropy = 1;
    staticSamplers[2].ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    staticSamplers[2].BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE;
    staticSamplers[2].MinLOD = 0;
    staticSamplers[2].MaxLOD = D3D12_FLOAT32_MAX;
    staticSamplers[2].ShaderRegister = 6; // s6
    staticSamplers[2].RegisterSpace = 0;
    staticSamplers[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    // s7: Linear clamp sampler for SSAO upsampling
    staticSamplers[3].Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    staticSamplers[3].AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    staticSamplers[3].AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    staticSamplers[3].AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    staticSamplers[3].MipLODBias = 0;
    staticSamplers[3].MaxAnisotropy = 1;
    staticSamplers[3].ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
    staticSamplers[3].BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_BLACK;
    staticSamplers[3].MinLOD = 0;
    staticSamplers[3].MaxLOD = D3D12_FLOAT32_MAX;
    staticSamplers[3].ShaderRegister = 7; // s7
    staticSamplers[3].RegisterSpace = 0;
    staticSamplers[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    // Root param 4: Forward+ SRV table (t7=AllLights, t8=LightGrid, t9=LightIndices, t10-t11=ReflectionProbes)
    D3D12_DESCRIPTOR_RANGE forwardPlusSrvRange = {};
    forwardPlusSrvRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    forwardPlusSrvRange.NumDescriptors = 5;     // t7, t8, t9, t10, t11
    forwardPlusSrvRange.BaseShaderRegister = 7; // t7
    forwardPlusSrvRange.RegisterSpace = 0;
    forwardPlusSrvRange.OffsetInDescriptorsFromTableStart = 0;

    rootParams[4].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParams[4].DescriptorTable.NumDescriptorRanges = 1;
    rootParams[4].DescriptorTable.pDescriptorRanges = &forwardPlusSrvRange;
    rootParams[4].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    // Root param 5: instance matrix cbuffer (b1) for hardware instancing path.
    rootParams[5].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParams[5].Descriptor.ShaderRegister = 1; // b1
    rootParams[5].Descriptor.RegisterSpace = 0;
    rootParams[5].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;

    D3D12_ROOT_SIGNATURE_DESC rootSigDesc = {};
    rootSigDesc.NumParameters = 6;
    rootSigDesc.pParameters = rootParams;
    rootSigDesc.NumStaticSamplers = 4;
    rootSigDesc.pStaticSamplers = staticSamplers;
    rootSigDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    ComPtr<ID3DBlob> sigBlob, errorBlob;
    hr = D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1, &sigBlob, &errorBlob);
    if (FAILED(hr))
    {
        if (errorBlob)
            std::printf("Root signature serialization error: %s\n", (char*)errorBlob->GetBufferPointer());
        std::printf("Root signature serialization failed: 0x%08X\n", hr);
        return false;
    }

    ID3D12RootSignature* rootSig = nullptr;
    hr = d3dDevice->CreateRootSignature(0, sigBlob->GetBufferPointer(), sigBlob->GetBufferSize(),
                                        IID_PPV_ARGS(&rootSig));
    if (FAILED(hr))
        return false;
    m_RootSignature = rootSig;

    // Create sampler heap with 9 samplers (3 filter x 3 wrap modes)
    D3D12_DESCRIPTOR_HEAP_DESC samplerHeapDesc = {};
    samplerHeapDesc.NumDescriptors = 9;
    samplerHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
    samplerHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

    ID3D12DescriptorHeap* samplerHeap = nullptr;
    hr = d3dDevice->CreateDescriptorHeap(&samplerHeapDesc, IID_PPV_ARGS(&samplerHeap));
    if (FAILED(hr))
        return false;
    m_SamplerHeap = samplerHeap;

    UINT samplerDescSize = d3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
    D3D12_CPU_DESCRIPTOR_HANDLE samplerHandle = samplerHeap->GetCPUDescriptorHandleForHeapStart();

    // Filter modes: 0=Point, 1=Linear, 2=Trilinear
    D3D12_FILTER filters[] = {D3D12_FILTER_MIN_MAG_MIP_POINT, D3D12_FILTER_MIN_MAG_LINEAR_MIP_POINT,
                              D3D12_FILTER_MIN_MAG_MIP_LINEAR};

    // Wrap modes: 0=Wrap, 1=Clamp, 2=Mirror
    D3D12_TEXTURE_ADDRESS_MODE wrapModes[] = {D3D12_TEXTURE_ADDRESS_MODE_WRAP, D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
                                              D3D12_TEXTURE_ADDRESS_MODE_MIRROR};

    for (int f = 0; f < 3; f++)
    {
        for (int w = 0; w < 3; w++)
        {
            D3D12_SAMPLER_DESC samplerDesc = {};
            samplerDesc.Filter = filters[f];
            samplerDesc.AddressU = wrapModes[w];
            samplerDesc.AddressV = wrapModes[w];
            samplerDesc.AddressW = wrapModes[w];
            samplerDesc.MipLODBias = 0;
            samplerDesc.MaxAnisotropy = 1;
            samplerDesc.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
            samplerDesc.MinLOD = 0;
            samplerDesc.MaxLOD = D3D12_FLOAT32_MAX;

            d3dDevice->CreateSampler(&samplerDesc, samplerHandle);
            samplerHandle.ptr += samplerDescSize;
        }
    }

    // Input layout: Position, Normal, Color, UV
    D3D12_INPUT_ELEMENT_DESC inputLayout[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 40, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 1, DXGI_FORMAT_R32G32_FLOAT, 0, 48, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"TANGENT", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 56, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    };

    // Pipeline state
    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.pRootSignature = rootSig;
    psoDesc.VS = {m_VSBytecode.data(), m_VSBytecode.size()};
    psoDesc.PS = {m_PSBytecode.data(), m_PSBytecode.size()};
    psoDesc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    psoDesc.SampleMask = UINT_MAX;
    psoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_BACK;
    psoDesc.RasterizerState.FrontCounterClockwise = FALSE;
    psoDesc.RasterizerState.DepthClipEnable = TRUE;
    psoDesc.DepthStencilState.DepthEnable = TRUE;
    psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    psoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
    psoDesc.InputLayout = {inputLayout, _countof(inputLayout)};
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets = 1;
    psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    psoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
    psoDesc.SampleDesc.Count = 1;

    ID3D12PipelineState* pso = nullptr;
    hr = d3dDevice->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&pso));
    if (FAILED(hr))
        return false;
    m_PipelineState = pso;

    // Create instanced PSO (same PS, instanced VS).
    if (!m_InstancedVSBytecode.empty())
    {
        D3D12_GRAPHICS_PIPELINE_STATE_DESC instancedDesc = psoDesc;
        instancedDesc.VS = {m_InstancedVSBytecode.data(), m_InstancedVSBytecode.size()};

        ID3D12PipelineState* instancedPso = nullptr;
        hr = d3dDevice->CreateGraphicsPipelineState(&instancedDesc, IID_PPV_ARGS(&instancedPso));
        if (FAILED(hr))
            return false;
        m_InstancedPipelineState = instancedPso;
    }

    // Create wireframe PSO (same as solid but with wireframe fill mode)
    psoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_WIREFRAME;
    psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE; // Show all edges
    ID3D12PipelineState* wireframePso = nullptr;
    hr = d3dDevice->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&wireframePso));
    if (FAILED(hr))
    {
        std::printf("Failed to create Wireframe PSO! HR=%08X\n", hr);
        return false;
    }
    m_WireframePSO = wireframePso;

    // Create depth visualization PSO (solid rendering with depth shader)
    psoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_BACK;
    psoDesc.PS = {m_DepthPSBytecode.data(), m_DepthPSBytecode.size()};
    ID3D12PipelineState* depthPso = nullptr;
    hr = d3dDevice->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&depthPso));
    if (FAILED(hr))
    {
        std::printf("Failed to create Depth PSO! HR=%08X\n", hr);
        return false;
    }
    m_DepthPSO = depthPso;

    // Create skybox root signature with CBV + SRV + Sampler
    std::printf("Creating Skybox Resources... VSSize=%zu PSSize=%zu\n", m_SkyboxVSBytecode.size(),
                m_SkyboxPSBytecode.size());

    if (!m_SkyboxVSBytecode.empty() && !m_SkyboxPSBytecode.empty())
    {
        // D3D12 address modes for each wrap mode index
        D3D12_TEXTURE_ADDRESS_MODE addressModes[kSkyboxWrapModeCount] = {
            D3D12_TEXTURE_ADDRESS_MODE_CLAMP, // 0
            D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // 1
            D3D12_TEXTURE_ADDRESS_MODE_MIRROR // 2
        };
        const char* wrapModeNames[] = {"Clamp", "Wrap", "Mirror"};

        // Create root signature and PSO for each wrap mode
        for (size_t wrapIdx = 0; wrapIdx < kSkyboxWrapModeCount; ++wrapIdx)
        {
            // Root parameter 0: CBV for view/projection matrix
            D3D12_ROOT_PARAMETER skyboxRootParams[2] = {};
            skyboxRootParams[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
            skyboxRootParams[0].Descriptor.ShaderRegister = 0;
            skyboxRootParams[0].Descriptor.RegisterSpace = 0;
            skyboxRootParams[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

            // Root parameter 1: SRV descriptor table for cubemap texture
            D3D12_DESCRIPTOR_RANGE srvRange = {};
            srvRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
            srvRange.NumDescriptors = 1;
            srvRange.BaseShaderRegister = 0;
            srvRange.RegisterSpace = 0;
            srvRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

            skyboxRootParams[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
            skyboxRootParams[1].DescriptorTable.NumDescriptorRanges = 1;
            skyboxRootParams[1].DescriptorTable.pDescriptorRanges = &srvRange;
            skyboxRootParams[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

            // Static sampler for cubemap - using the wrap mode for this index
            D3D12_STATIC_SAMPLER_DESC staticSampler = {};
            staticSampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
            staticSampler.AddressU = addressModes[wrapIdx];
            staticSampler.AddressV = addressModes[wrapIdx];
            staticSampler.AddressW = addressModes[wrapIdx];
            staticSampler.MipLODBias = 0.0f;
            staticSampler.MaxAnisotropy = 1;
            staticSampler.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
            staticSampler.BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_BLACK;
            staticSampler.MinLOD = 0.0f;
            staticSampler.MaxLOD = D3D12_FLOAT32_MAX;
            staticSampler.ShaderRegister = 0;
            staticSampler.RegisterSpace = 0;
            staticSampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

            D3D12_ROOT_SIGNATURE_DESC skyboxRootSigDesc = {};
            skyboxRootSigDesc.NumParameters = 2;
            skyboxRootSigDesc.pParameters = skyboxRootParams;
            skyboxRootSigDesc.NumStaticSamplers = 1;
            skyboxRootSigDesc.pStaticSamplers = &staticSampler;
            skyboxRootSigDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

            ComPtr<ID3DBlob> skyboxSigBlob;
            hr = D3D12SerializeRootSignature(&skyboxRootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1, &skyboxSigBlob,
                                             &errorBlob);
            if (FAILED(hr))
            {
                std::printf("Failed to serialize Skybox Root Signature [%s]! HR=%08X Error=%s\n",
                            wrapModeNames[wrapIdx], hr, errorBlob ? (char*)errorBlob->GetBufferPointer() : "Unknown");
                return false;
            }

            ID3D12RootSignature* skyboxRootSig = nullptr;
            hr = d3dDevice->CreateRootSignature(0, skyboxSigBlob->GetBufferPointer(), skyboxSigBlob->GetBufferSize(),
                                                IID_PPV_ARGS(&skyboxRootSig));
            if (FAILED(hr))
            {
                std::printf("Failed to create Skybox Root Signature [%s]! HR=%08X\n", wrapModeNames[wrapIdx], hr);
                return false;
            }
            m_SkyboxRootSignature[wrapIdx] = skyboxRootSig;

            // Create skybox PSO for this wrap mode
            D3D12_GRAPHICS_PIPELINE_STATE_DESC skyboxPsoDesc = {};
            skyboxPsoDesc.pRootSignature = skyboxRootSig;
            skyboxPsoDesc.VS = {m_SkyboxVSBytecode.data(), m_SkyboxVSBytecode.size()};
            skyboxPsoDesc.PS = {m_SkyboxPSBytecode.data(), m_SkyboxPSBytecode.size()};
            skyboxPsoDesc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
            skyboxPsoDesc.SampleMask = UINT_MAX;
            skyboxPsoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
            skyboxPsoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_FRONT;
            skyboxPsoDesc.RasterizerState.FrontCounterClockwise = FALSE;
            skyboxPsoDesc.RasterizerState.DepthClipEnable = TRUE;
            skyboxPsoDesc.DepthStencilState.DepthEnable = TRUE;
            skyboxPsoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
            skyboxPsoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
            skyboxPsoDesc.InputLayout = {inputLayout, _countof(inputLayout)};
            skyboxPsoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
            skyboxPsoDesc.NumRenderTargets = 1;
            skyboxPsoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
            skyboxPsoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
            skyboxPsoDesc.SampleDesc.Count = 1;

            ID3D12PipelineState* skyboxPso = nullptr;
            hr = d3dDevice->CreateGraphicsPipelineState(&skyboxPsoDesc, IID_PPV_ARGS(&skyboxPso));
            if (FAILED(hr))
            {
                std::printf("Failed to create Skybox PSO [%s]! HR=%08X\n", wrapModeNames[wrapIdx], hr);
                return false;
            }
            m_SkyboxPSO[wrapIdx] = skyboxPso;

            std::printf("Created Skybox PSO [%s]\n", wrapModeNames[wrapIdx]);
        }

        // Create SRV descriptor heap for skybox cubemap (only need one)
        D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
        srvHeapDesc.NumDescriptors = 1;
        srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        ID3D12DescriptorHeap* srvHeap = nullptr;
        hr = d3dDevice->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&srvHeap));
        if (FAILED(hr))
        {
            std::printf("Failed to create Skybox SRV Heap! HR=%08X\n", hr);
            return false;
        }
        m_SkyboxSRVHeap = srvHeap;
    }

    return true;
}

// ...

bool SimpleRenderer::CreateAllMeshes()
{
    // Generate mesh data for each primitive type
    MeshData meshData[kPrimitiveCount];
    MeshData authoredLodData[kPrimitiveCount][PrimitiveMesh::kLodCount];
    meshData[static_cast<size_t>(PrimitiveType::Cube)] = GenerateCube();
    meshData[static_cast<size_t>(PrimitiveType::Sphere)] = GenerateSphere();
    meshData[static_cast<size_t>(PrimitiveType::Cylinder)] = GenerateCylinder();
    meshData[static_cast<size_t>(PrimitiveType::Plane)] = GeneratePlane();
    meshData[static_cast<size_t>(PrimitiveType::Cone)] = GenerateCone();
    meshData[static_cast<size_t>(PrimitiveType::Capsule)] = GenerateCapsule();

    authoredLodData[static_cast<size_t>(PrimitiveType::Sphere)][1] = GenerateSphere(12, 24);
    authoredLodData[static_cast<size_t>(PrimitiveType::Sphere)][2] = GenerateSphere(8, 16);
    authoredLodData[static_cast<size_t>(PrimitiveType::Cylinder)][1] = GenerateCylinder(16);
    authoredLodData[static_cast<size_t>(PrimitiveType::Cylinder)][2] = GenerateCylinder(10);
    authoredLodData[static_cast<size_t>(PrimitiveType::Cone)][1] = GenerateCone(14);
    authoredLodData[static_cast<size_t>(PrimitiveType::Cone)][2] = GenerateCone(8);
    authoredLodData[static_cast<size_t>(PrimitiveType::Capsule)][1] = GenerateCapsule(12, 6);
    authoredLodData[static_cast<size_t>(PrimitiveType::Capsule)][2] = GenerateCapsule(8, 4);

    // Create buffers for each mesh
    for (size_t i = 0; i < kPrimitiveCount; ++i)
    {
        const auto& data = meshData[i];
        auto& mesh = m_Meshes[i];
        if (!CreateMeshBuffers(data, mesh))
            return false;

        ConfigurePrimitiveLodPolicy(static_cast<PrimitiveType>(i), mesh);
        mesh.submeshes = data.submeshes;

        for (uint32_t lodLevel = 1; lodLevel < PrimitiveMesh::kLodCount; ++lodLevel)
        {
            const MeshData& authoredLod = authoredLodData[i][lodLevel];
            if (!authoredLod.vertices.empty() && !authoredLod.indices.empty())
                ApplyAuthoredPrimitiveLod(*this, mesh, lodLevel, authoredLod);
        }
    }

    std::printf("Created %zu primitive meshes\n", kPrimitiveCount);
    return true;
}

bool SimpleRenderer::CreateMeshBuffers(const MeshData& meshData, PrimitiveMesh& outMesh)
{
    if (meshData.vertices.empty() || meshData.indices.empty())
        return false;

    MeshData resolvedMeshData = meshData;
    RecomputeMeshBounds(resolvedMeshData);
    ComputeMeshTangents(resolvedMeshData);

    // Vertex buffer
    RHIBufferDesc vbDesc;
    vbDesc.size = resolvedMeshData.vertices.size() * sizeof(PrimitiveVertex);
    vbDesc.usage = RHIBufferUsage::Vertex;
    vbDesc.memory = RHIMemoryUsage::GPU_Only;

    outMesh.vertexBuffer = m_Device->CreateBuffer(vbDesc);
    if (!outMesh.vertexBuffer)
        return false;
    outMesh.vertexBuffer->Update(resolvedMeshData.vertices.data(), vbDesc.size);

    // Index buffer
    RHIBufferDesc ibDesc;
    ibDesc.size = resolvedMeshData.indices.size() * sizeof(uint32_t);
    ibDesc.usage = RHIBufferUsage::Index;
    ibDesc.memory = RHIMemoryUsage::GPU_Only;

    outMesh.indexBuffer = m_Device->CreateBuffer(ibDesc);
    if (!outMesh.indexBuffer)
        return false;
    outMesh.indexBuffer->Update(resolvedMeshData.indices.data(), ibDesc.size);

    outMesh.indexCount = static_cast<uint32_t>(resolvedMeshData.indices.size());
    outMesh.lodVertexBuffers[0] = outMesh.vertexBuffer;
    outMesh.lodVertexCounts[0] = static_cast<uint32_t>(resolvedMeshData.vertices.size());
    outMesh.lodIndexBuffers[0] = outMesh.indexBuffer;
    outMesh.lodIndexCounts[0] = outMesh.indexCount;
    outMesh.lodSubmeshes[0] = resolvedMeshData.submeshes;
    BuildMeshRenderBatches(outMesh.lodSubmeshes[0], outMesh.lodIndexCounts[0], outMesh.lodRenderBatches[0],
                           outMesh.lodHasTexturedSubmeshes[0]);
    ResolveMeshRenderBatchTextures(
        outMesh.lodSubmeshes[0], outMesh.lodRenderBatches[0],
        [this](const std::string& path) -> AssetHandle<TextureAsset> {
            if (TextureCacheEntry* tex = LoadMaterialTexture(path, TextureSemantic::Color))
                return tex->handle;
            return {};
        });

    const float lodTargetRatios[PrimitiveMesh::kLodCount] = {1.0f, 0.55f, 0.25f};

    for (uint32_t lodLevel = 1; lodLevel < PrimitiveMesh::kLodCount; ++lodLevel)
    {
        outMesh.lodVertexBuffers[lodLevel] = outMesh.vertexBuffer;
        outMesh.lodVertexCounts[lodLevel] = static_cast<uint32_t>(resolvedMeshData.vertices.size());
        outMesh.lodIndexBuffers[lodLevel] = outMesh.indexBuffer;
        outMesh.lodIndexCounts[lodLevel] = outMesh.indexCount;
        outMesh.lodSubmeshes[lodLevel] = resolvedMeshData.submeshes;
        BuildMeshRenderBatches(outMesh.lodSubmeshes[lodLevel], outMesh.lodIndexCounts[lodLevel],
                               outMesh.lodRenderBatches[lodLevel], outMesh.lodHasTexturedSubmeshes[lodLevel]);
        ResolveMeshRenderBatchTextures(
            outMesh.lodSubmeshes[lodLevel], outMesh.lodRenderBatches[lodLevel],
            [this](const std::string& path) -> AssetHandle<TextureAsset> {
                if (TextureCacheEntry* tex = LoadMaterialTexture(path, TextureSemantic::Color))
                    return tex->handle;
                return {};
            });

        MeshData lodMeshData;
        if (!BuildClusterDecimatedMesh(resolvedMeshData, lodTargetRatios[lodLevel], lodMeshData))
            continue;
        ComputeMeshTangents(lodMeshData);

        RHIBufferDesc lodVbDesc;
        lodVbDesc.size = lodMeshData.vertices.size() * sizeof(PrimitiveVertex);
        lodVbDesc.usage = RHIBufferUsage::Vertex;
        lodVbDesc.memory = RHIMemoryUsage::GPU_Only;

        RHIBufferPtr lodVertexBuffer = m_Device->CreateBuffer(lodVbDesc);
        if (!lodVertexBuffer)
            continue;
        lodVertexBuffer->Update(lodMeshData.vertices.data(), lodVbDesc.size);

        RHIBufferDesc lodIbDesc;
        lodIbDesc.size = lodMeshData.indices.size() * sizeof(uint32_t);
        lodIbDesc.usage = RHIBufferUsage::Index;
        lodIbDesc.memory = RHIMemoryUsage::GPU_Only;

        RHIBufferPtr lodIndexBuffer = m_Device->CreateBuffer(lodIbDesc);
        if (!lodIndexBuffer)
            continue;
        lodIndexBuffer->Update(lodMeshData.indices.data(), lodIbDesc.size);

        outMesh.lodVertexBuffers[lodLevel] = lodVertexBuffer;
        outMesh.lodVertexCounts[lodLevel] = static_cast<uint32_t>(lodMeshData.vertices.size());
        outMesh.lodIndexBuffers[lodLevel] = lodIndexBuffer;
        outMesh.lodIndexCounts[lodLevel] = static_cast<uint32_t>(lodMeshData.indices.size());
        outMesh.lodSubmeshes[lodLevel] = std::move(lodMeshData.submeshes);
        BuildMeshRenderBatches(outMesh.lodSubmeshes[lodLevel], outMesh.lodIndexCounts[lodLevel],
                               outMesh.lodRenderBatches[lodLevel], outMesh.lodHasTexturedSubmeshes[lodLevel]);
        ResolveMeshRenderBatchTextures(
            outMesh.lodSubmeshes[lodLevel], outMesh.lodRenderBatches[lodLevel],
            [this](const std::string& path) -> AssetHandle<TextureAsset> {
                if (TextureCacheEntry* tex = LoadMaterialTexture(path, TextureSemantic::Color))
                    return tex->handle;
                return {};
            });
    }

    // Copy bounds from mesh data
    outMesh.boundsMinX = resolvedMeshData.boundsMinX;
    outMesh.boundsMinY = resolvedMeshData.boundsMinY;
    outMesh.boundsMinZ = resolvedMeshData.boundsMinZ;
    outMesh.boundsMaxX = resolvedMeshData.boundsMaxX;
    outMesh.boundsMaxY = resolvedMeshData.boundsMaxY;
    outMesh.boundsMaxZ = resolvedMeshData.boundsMaxZ;

    return true;
}

std::unique_ptr<PrimitiveMesh> SimpleRenderer::CreateRuntimeMesh(const MeshData& meshData)
{
    auto mesh = std::make_unique<PrimitiveMesh>();
    if (!CreateMeshBuffers(meshData, *mesh))
        return nullptr;

    mesh->submeshes = meshData.submeshes;
    return mesh;
}

bool SimpleRenderer::CreateConstantBuffer()
{
    // CBV is now created in CreatePipelineState
    return true;
}

bool SimpleRenderer::CreateShadowResources()
{
    ID3D12Device* d3dDevice = GetD3D12DevicePtr(m_Device);
    if (!d3dDevice)
        return false;

    // ---- Create Shadow Depth Texture ----
    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

    D3D12_RESOURCE_DESC texDesc = {};
    texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    texDesc.Width = kShadowMapResolution;
    texDesc.Height = kShadowMapResolution;
    texDesc.DepthOrArraySize = 1;
    texDesc.MipLevels = 1;
    texDesc.Format = DXGI_FORMAT_R32_TYPELESS; // Typeless for DSV/SRV compatibility
    texDesc.SampleDesc.Count = 1;
    texDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

    D3D12_CLEAR_VALUE clearValue = {};
    clearValue.Format = DXGI_FORMAT_D32_FLOAT;
    clearValue.DepthStencil.Depth = 1.0f;
    clearValue.DepthStencil.Stencil = 0;

    ID3D12Resource* shadowTex = nullptr;
    HRESULT hr =
        d3dDevice->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &texDesc, D3D12_RESOURCE_STATE_DEPTH_WRITE,
                                           &clearValue, IID_PPV_ARGS(&shadowTex));
    if (FAILED(hr))
    {
        std::printf("Failed to create shadow depth texture: 0x%08X\n", hr);
        return false;
    }
    m_ShadowDepthBuffer = shadowTex;

    // ---- Create DSV Heap ----
    D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc = {};
    dsvHeapDesc.NumDescriptors = 1;
    dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    dsvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

    ID3D12DescriptorHeap* dsvHeap = nullptr;
    hr = d3dDevice->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(&dsvHeap));
    if (FAILED(hr))
    {
        std::printf("Failed to create shadow DSV heap: 0x%08X\n", hr);
        return false;
    }
    m_ShadowDSVHeap = dsvHeap;

    // Create DSV
    D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
    dsvDesc.Format = DXGI_FORMAT_D32_FLOAT;
    dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    d3dDevice->CreateDepthStencilView(shadowTex, &dsvDesc, dsvHeap->GetCPUDescriptorHandleForHeapStart());

    // ---- Create Local Shadow Depth Array ----
    D3D12_RESOURCE_DESC localTexDesc = texDesc;
    localTexDesc.Width = kLocalShadowResolution;
    localTexDesc.Height = kLocalShadowResolution;
    localTexDesc.DepthOrArraySize = static_cast<UINT16>(kMaxLocalShadowSlices);

    ID3D12Resource* localShadowTex = nullptr;
    hr = d3dDevice->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &localTexDesc,
                                            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, &clearValue,
                                            IID_PPV_ARGS(&localShadowTex));
    if (FAILED(hr))
    {
        std::printf("Failed to create local shadow depth texture: 0x%08X\n", hr);
        return false;
    }
    m_LocalShadowDepthBuffer = localShadowTex;

    D3D12_DESCRIPTOR_HEAP_DESC localDsvHeapDesc = {};
    localDsvHeapDesc.NumDescriptors = kMaxLocalShadowSlices;
    localDsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    localDsvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

    ID3D12DescriptorHeap* localDsvHeap = nullptr;
    hr = d3dDevice->CreateDescriptorHeap(&localDsvHeapDesc, IID_PPV_ARGS(&localDsvHeap));
    if (FAILED(hr))
    {
        std::printf("Failed to create local shadow DSV heap: 0x%08X\n", hr);
        return false;
    }
    m_LocalShadowDSVHeap = localDsvHeap;

    const UINT dsvSize = d3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
    for (uint32_t slice = 0; slice < kMaxLocalShadowSlices; ++slice)
    {
        D3D12_DEPTH_STENCIL_VIEW_DESC sliceDsvDesc = {};
        sliceDsvDesc.Format = DXGI_FORMAT_D32_FLOAT;
        sliceDsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DARRAY;
        sliceDsvDesc.Texture2DArray.ArraySize = 1;
        sliceDsvDesc.Texture2DArray.FirstArraySlice = slice;
        D3D12_CPU_DESCRIPTOR_HANDLE sliceHandle = localDsvHeap->GetCPUDescriptorHandleForHeapStart();
        sliceHandle.ptr += static_cast<SIZE_T>(slice) * dsvSize;
        d3dDevice->CreateDepthStencilView(localShadowTex, &sliceDsvDesc, sliceHandle);
    }

    // ---- Create SRV Heap ----
    D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
    srvHeapDesc.NumDescriptors = 12; // Support t4-t11 for shadowing, Forward+, and reflection probes
    srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

    ID3D12DescriptorHeap* srvHeap = nullptr;
    hr = d3dDevice->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&srvHeap));
    if (FAILED(hr))
    {
        std::printf("Failed to create shadow SRV heap: 0x%08X\n", hr);
        return false;
    }
    m_ShadowSRVHeap = srvHeap;

    // Create SRV for sampling shadow map.
    // Slots 4-11 are reserved for:
    // t4 ShadowMap, t5 SSAO, t6 LocalShadowMap, t7 AllLights, t8 LightGrid, t9 LightIndices, t10-t11 ReflectionProbes.
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = DXGI_FORMAT_R32_FLOAT;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Texture2D.MipLevels = 1;

    // Fill slot 4 (for t4)
    UINT srvSize = d3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle = srvHeap->GetCPUDescriptorHandleForHeapStart();
    cpuHandle.ptr += 4 * srvSize;
    d3dDevice->CreateShaderResourceView(shadowTex, &srvDesc, cpuHandle);

    D3D12_SHADER_RESOURCE_VIEW_DESC localSrvDesc = {};
    localSrvDesc.Format = DXGI_FORMAT_R32_FLOAT;
    localSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
    localSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    localSrvDesc.Texture2DArray.MipLevels = 1;
    localSrvDesc.Texture2DArray.ArraySize = kMaxLocalShadowSlices;
    localSrvDesc.Texture2DArray.FirstArraySlice = 0;
    D3D12_CPU_DESCRIPTOR_HANDLE localCpuHandle = srvHeap->GetCPUDescriptorHandleForHeapStart();
    localCpuHandle.ptr += 6 * srvSize;
    d3dDevice->CreateShaderResourceView(localShadowTex, &localSrvDesc, localCpuHandle);

    RefreshGraphInteropTextures();

    // ---- Compile Shadow Depth Vertex Shader ----
    const char* shadowVsCode = R"(
        cbuffer ShadowCB : register(b0) {
            float4x4 LightMVP;
        };
        struct VSInput {
            float3 Position : POSITION;
            float3 Normal : NORMAL;
            float4 Tangent : TANGENT;
            float4 Color : COLOR;
            float2 UV : TEXCOORD0;
            float2 UV2 : TEXCOORD1;
        };
        struct VSOutput {
            float4 Position : SV_POSITION;
        };
        VSOutput VSMain(VSInput input) {
            VSOutput output;
            output.Position = mul(LightMVP, float4(input.Position, 1.0f));
            return output;
        }
    )";

    ComPtr<ID3DBlob> vsBlob, errorBlob;
    hr = D3DCompile(shadowVsCode, strlen(shadowVsCode), "ShadowVS", nullptr, nullptr, "VSMain", "vs_5_0",
                    D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &vsBlob, &errorBlob);
    if (FAILED(hr))
    {
        if (errorBlob)
            std::printf("Shadow VS compile error: %s\n", (char*)errorBlob->GetBufferPointer());
        return false;
    }

    m_ShadowVSBytecode.resize(vsBlob->GetBufferSize());
    memcpy(m_ShadowVSBytecode.data(), vsBlob->GetBufferPointer(), vsBlob->GetBufferSize());

    // ---- Create Shadow Root Signature (simple CBV only) ----
    D3D12_ROOT_PARAMETER rootParam = {};
    rootParam.ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParam.Descriptor.ShaderRegister = 0;
    rootParam.Descriptor.RegisterSpace = 0;
    rootParam.ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;

    D3D12_ROOT_SIGNATURE_DESC rsDesc = {};
    rsDesc.NumParameters = 1;
    rsDesc.pParameters = &rootParam;
    rsDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    ComPtr<ID3DBlob> rsBlob;
    hr = D3D12SerializeRootSignature(&rsDesc, D3D_ROOT_SIGNATURE_VERSION_1, &rsBlob, &errorBlob);
    if (FAILED(hr))
    {
        if (errorBlob)
            std::printf("Shadow RS serialize error: %s\n", (char*)errorBlob->GetBufferPointer());
        return false;
    }

    ID3D12RootSignature* shadowRS = nullptr;
    hr =
        d3dDevice->CreateRootSignature(0, rsBlob->GetBufferPointer(), rsBlob->GetBufferSize(), IID_PPV_ARGS(&shadowRS));
    if (FAILED(hr))
    {
        std::printf("Failed to create shadow root signature: 0x%08X\n", hr);
        return false;
    }
    m_ShadowRootSignature = shadowRS;

    // ---- Create Shadow PSO (depth-only, no pixel shader) ----
    D3D12_INPUT_ELEMENT_DESC inputLayout[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 40, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 1, DXGI_FORMAT_R32G32_FLOAT, 0, 48, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"TANGENT", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 56, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    };

    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.pRootSignature = shadowRS;
    psoDesc.VS = {m_ShadowVSBytecode.data(), m_ShadowVSBytecode.size()};
    psoDesc.PS = {nullptr, 0}; // No pixel shader - depth only
    psoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_BACK;
    psoDesc.RasterizerState.FrontCounterClockwise = FALSE;
    psoDesc.RasterizerState.DepthBias = 1000; // Depth bias to reduce shadow acne
    psoDesc.RasterizerState.DepthBiasClamp = 0.0f;
    psoDesc.RasterizerState.SlopeScaledDepthBias = 1.0f;
    psoDesc.RasterizerState.DepthClipEnable = TRUE;
    psoDesc.DepthStencilState.DepthEnable = TRUE;
    psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    psoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
    psoDesc.SampleMask = UINT_MAX;
    psoDesc.InputLayout = {inputLayout, _countof(inputLayout)};
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets = 0; // Depth only
    psoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
    psoDesc.SampleDesc.Count = 1;

    ID3D12PipelineState* shadowPSO = nullptr;
    hr = d3dDevice->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&shadowPSO));
    if (FAILED(hr))
    {
        std::printf("Failed to create shadow PSO: 0x%08X\n", hr);
        return false;
    }
    m_ShadowPSO = shadowPSO;

    std::printf("Shadow mapping resources created (%dx%d depth texture)\n", kShadowMapResolution, kShadowMapResolution);
    return true;
}

// Load or retrieve a cached material texture via AssetManager
SimpleRenderer::TextureCacheEntry* SimpleRenderer::LoadMaterialTexture(const std::string& path, TextureSemantic semantic)
{
    if (path.empty())
        return nullptr;

    if (semantic == TextureSemantic::Auto)
        semantic = GuessTextureSemanticFromPath(path);

    const std::string cacheKey = BuildTextureSemanticCacheKey(path, semantic);
    auto it = m_TextureCache.find(cacheKey);
    if (it != m_TextureCache.end())
    {
        return &it->second;
    }

    // Request from AssetManager - create new entry
    TextureCacheEntry& entry = m_TextureCache[cacheKey];
    entry.handle = AssetManager::Get().LoadTexture(path, semantic);
    entry.srvHeap = nullptr;

    return &entry;
}

SimpleRenderer::CubemapCacheEntry* SimpleRenderer::LoadCubemapTexture(const std::string& path)
{
    if (path.empty() || !m_Device)
        return nullptr;

    const uint64_t sourceStamp = ComputeCubemapSourceStamp(path);
    auto it = m_CubemapCache.find(path);
    if (it != m_CubemapCache.end())
    {
        if (it->second.texture && (sourceStamp == 0 || it->second.sourceStamp == sourceStamp))
            return &it->second;
        m_CubemapCache.erase(it);
    }

    CubemapData cubemapData = LoadCubemap(path);
    if (!cubemapData.IsValid())
        return nullptr;

    const uint32_t mipLevels =
        cubemapData.IsHdr()
            ? 1u
            : ComputeTextureMipCount(static_cast<uint32_t>(cubemapData.width), static_cast<uint32_t>(cubemapData.height));

    RHITextureDesc texDesc;
    texDesc.width = static_cast<uint32_t>(cubemapData.width);
    texDesc.height = static_cast<uint32_t>(cubemapData.height);
    texDesc.depth = 1;
    texDesc.arrayLayers = CubemapData::FACE_COUNT;
    texDesc.mipLevels = mipLevels;
    texDesc.format = cubemapData.format;
    texDesc.type = RHITextureType::TextureCube;
    texDesc.usage = RHITextureUsage::Sampled;
    texDesc.debugName = "ReflectionProbeCubemap";

    CubemapCacheEntry cacheEntry;
    cacheEntry.mipLevels = mipLevels;
    cacheEntry.sourceStamp = sourceStamp;
    cacheEntry.texture = m_Device->CreateTexture(texDesc);
    if (!cacheEntry.texture)
        return nullptr;

    ID3D12Device* d3dDevice = GetD3D12DevicePtr(m_Device);
    ID3D12GraphicsCommandList* cmdList = GetD3D12CommandList(m_Device);
    D3D12Texture* d3dTexture = static_cast<D3D12Texture*>(cacheEntry.texture.get());
    ID3D12Resource* textureRes = d3dTexture ? d3dTexture->GetResource() : nullptr;
    if (!d3dDevice || !cmdList || !textureRes)
        return nullptr;

    static std::vector<ComPtr<ID3D12Resource>> s_ProbeUploadBuffers;
    if (!UploadCubemapTexture(d3dDevice, cmdList, textureRes, cubemapData, mipLevels, s_ProbeUploadBuffers))
        return nullptr;

    auto [insertedIt, inserted] = m_CubemapCache.emplace(path, std::move(cacheEntry));
    return insertedIt->second.texture ? &insertedIt->second : nullptr;
}

void SimpleRenderer::WriteReflectionProbeDescriptors(void* descriptorHeap, uint32_t descriptorIndexBase)
{
    if (!descriptorHeap || !m_Device)
        return;

    ID3D12DescriptorHeap* heap = static_cast<ID3D12DescriptorHeap*>(descriptorHeap);
    ID3D12Device* d3dDevice = GetD3D12DevicePtr(m_Device);
    if (!heap || !d3dDevice)
        return;

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.TextureCube.MostDetailedMip = 0;
    srvDesc.TextureCube.MipLevels = 1;
    srvDesc.TextureCube.ResourceMinLODClamp = 0.0f;

    std::string descriptorSignature;
    descriptorSignature.reserve(256);
    for (uint32_t probeIndex = 0; probeIndex < kMaxReflectionProbeBlendCount; ++probeIndex)
    {
        if (m_ReflectionProbeData[probeIndex].enabled && !m_ReflectionProbeData[probeIndex].cubemapPath.empty())
        {
            descriptorSignature += m_ReflectionProbeData[probeIndex].cubemapPath;
            descriptorSignature += "#";
            descriptorSignature +=
                std::to_string(ComputeCubemapSourceStamp(m_ReflectionProbeData[probeIndex].cubemapPath));
        }
        else
            descriptorSignature += "<null>";
        descriptorSignature += "|";
    }

    auto signatureIt = m_ReflectionProbeDescriptorSignatures.find(heap);
    if (signatureIt != m_ReflectionProbeDescriptorSignatures.end() && signatureIt->second == descriptorSignature)
        return;
    m_ReflectionProbeDescriptorSignatures[heap] = descriptorSignature;

    const UINT srvSize = d3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    for (uint32_t probeIndex = 0; probeIndex < kMaxReflectionProbeBlendCount; ++probeIndex)
    {
        D3D12_CPU_DESCRIPTOR_HANDLE dest = heap->GetCPUDescriptorHandleForHeapStart();
        dest.ptr += static_cast<SIZE_T>(descriptorIndexBase + probeIndex) * srvSize;

        if (!m_ReflectionProbeData[probeIndex].enabled || m_ReflectionProbeData[probeIndex].cubemapPath.empty())
        {
            d3dDevice->CreateShaderResourceView(nullptr, &srvDesc, dest);
            continue;
        }

        CubemapCacheEntry* cubemap = LoadCubemapTexture(m_ReflectionProbeData[probeIndex].cubemapPath);
        if (!cubemap || !cubemap->texture)
        {
            d3dDevice->CreateShaderResourceView(nullptr, &srvDesc, dest);
            continue;
        }

        srvDesc.Format = GetTextureSrvFormat(cubemap->texture->GetFormat());
        srvDesc.TextureCube.MipLevels = cubemap->texture->GetMipLevels();
        ID3D12Resource* d3dResource =
            static_cast<ID3D12Resource*>(m_Device->GetNativeTextureResource(cubemap->texture.get()));
        d3dDevice->CreateShaderResourceView(d3dResource, &srvDesc, dest);
    }
}

void SimpleRenderer::RenderMesh(const Camera& camera, RHISwapChain* swapChain, const float* worldMatrix,
                                const PrimitiveMesh& mesh)
{
    if (!m_Initialized || !swapChain || !worldMatrix || mesh.indexCount == 0)
        return;

    ID3D12GraphicsCommandList* cmdList = GetD3D12CommandList(m_Device);
    if (!cmdList)
        return; // Null check for command list
    D3D12SwapChain* d3dSwapChain = static_cast<D3D12SwapChain*>(swapChain);

    // Multiply worldMatrix * ViewProjection to get MVP (column-major)
    const float* vp = camera.GetViewProjectionMatrix();
    float mvp[16];
    for (int col = 0; col < 4; ++col)
    {
        for (int row = 0; row < 4; ++row)
        {
            mvp[col * 4 + row] =
                vp[0 * 4 + row] * worldMatrix[col * 4 + 0] + vp[1 * 4 + row] * worldMatrix[col * 4 + 1] +
                vp[2 * 4 + row] * worldMatrix[col * 4 + 2] + vp[3 * 4 + row] * worldMatrix[col * 4 + 3];
        }
    }

    // Set viewport
    D3D12_VIEWPORT viewport = {};
    viewport.Width = static_cast<float>(swapChain->GetWidth());
    viewport.Height = static_cast<float>(swapChain->GetHeight());
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;
    cmdList->RSSetViewports(1, &viewport);

    // Set scissor rect
    D3D12_RECT scissor = {};
    scissor.right = static_cast<LONG>(swapChain->GetWidth());
    scissor.bottom = static_cast<LONG>(swapChain->GetHeight());
    cmdList->RSSetScissorRects(1, &scissor);

    // Set render targets
    D3D12_CPU_DESCRIPTOR_HANDLE rtv =
        m_IsInFXAAPass ? reinterpret_cast<ID3D12DescriptorHeap*>(m_FXAARTVHeap)->GetCPUDescriptorHandleForHeapStart()
                       : d3dSwapChain->GetCurrentRTV();
    D3D12_CPU_DESCRIPTOR_HANDLE dsv = d3dSwapChain->GetDSV();
    cmdList->OMSetRenderTargets(1, &rtv, FALSE, &dsv);

    // Choose PSO based on render mode and material
    auto& viewSettings = ViewSettings::Get();
    ID3D12PipelineState* pso = reinterpret_cast<ID3D12PipelineState*>(m_PipelineState);

    if (m_IsDepthPrepass)
    {
        // Depth prepass reuses shadow PSO - make sure it exists
        if (!m_ShadowPSO || !m_ShadowRootSignature)
            return; // Can't do depth prepass without shadow PSO
        pso = reinterpret_cast<ID3D12PipelineState*>(m_ShadowPSO);
        cmdList->SetGraphicsRootSignature(reinterpret_cast<ID3D12RootSignature*>(m_ShadowRootSignature));
    }
    else
    {
        cmdList->SetGraphicsRootSignature(reinterpret_cast<ID3D12RootSignature*>(m_RootSignature));

        // Check for custom procedural material first
        if (m_MaterialData.materialShaderId != 0)
        {
            auto it = m_CustomPSOs.find(m_MaterialData.materialShaderId);
            if (it != m_CustomPSOs.end() && it->second.pso != nullptr)
            {
                pso = reinterpret_cast<ID3D12PipelineState*>(it->second.pso);
            }
        }
        else if (viewSettings.debugVisMode == DebugVisMode::Wireframe)
            pso = reinterpret_cast<ID3D12PipelineState*>(m_WireframePSO);
        else if (viewSettings.debugVisMode == DebugVisMode::Depth)
            pso = reinterpret_cast<ID3D12PipelineState*>(m_DepthPSO);
    }

    cmdList->SetPipelineState(pso);

    // Check if we have enough space in the ring buffer
    const uint32_t alignedSize = (sizeof(ConstantBufferData) + 255) & ~255;

    if (m_CurrentCBOffset + alignedSize > kMaxCBSize)
    {
        return;
    }

    // Prepare constant buffer data using struct for correct alignment
    ConstantBufferData cbData = {};

    // Matrices
    memcpy(cbData.mvpMatrix, mvp, 16 * sizeof(float));
    memcpy(cbData.worldMatrix, worldMatrix, 16 * sizeof(float));

    // Directional Light
    cbData.dirLightDir[0] = m_LightData.lightDirX;
    cbData.dirLightDir[1] = m_LightData.lightDirY;
    cbData.dirLightDir[2] = m_LightData.lightDirZ;
    cbData.dirLightIntensity = m_LightData.lightIntensity;
    cbData.dirLightColor[0] = m_LightData.lightColorR;
    cbData.dirLightColor[1] = m_LightData.lightColorG;
    cbData.dirLightColor[2] = m_LightData.lightColorB;
    cbData._pad1 = 0.0f;

    // Ambient
    cbData.ambientColor[0] = m_LightData.ambientColorR;
    cbData.ambientColor[1] = m_LightData.ambientColorG;
    cbData.ambientColor[2] = m_LightData.ambientColorB;
    cbData.ambientIntensity = m_LightData.ambientIntensity;

    // Light counts
    cbData.numPointLights = m_LightData.numPointLights;
    cbData.numSpotLights = m_LightData.numSpotLights;

    // Point lights
    for (int i = 0; i < 16; ++i)
    {
        cbData.pointLights[i].posX = m_LightData.pointLights[i].posX;
        cbData.pointLights[i].posY = m_LightData.pointLights[i].posY;
        cbData.pointLights[i].posZ = m_LightData.pointLights[i].posZ;
        cbData.pointLights[i].range = m_LightData.pointLights[i].range;
        cbData.pointLights[i].colorR = m_LightData.pointLights[i].colorR;
        cbData.pointLights[i].colorG = m_LightData.pointLights[i].colorG;
        cbData.pointLights[i].colorB = m_LightData.pointLights[i].colorB;
        cbData.pointLights[i].intensity = m_LightData.pointLights[i].intensity;
        cbData.pointLights[i].shadowEnabled = m_LightData.pointLights[i].shadowEnabled;
        cbData.pointLights[i].shadowBaseSlice = m_LightData.pointLights[i].shadowBaseSlice;
        cbData.pointLights[i].shadowBias = m_LightData.pointLights[i].shadowBias;
        cbData.pointLights[i]._pad = 0.0f;
    }

    // Spot lights
    for (int i = 0; i < 16; ++i)
    {
        cbData.spotLights[i].posX = m_LightData.spotLights[i].posX;
        cbData.spotLights[i].posY = m_LightData.spotLights[i].posY;
        cbData.spotLights[i].posZ = m_LightData.spotLights[i].posZ;
        cbData.spotLights[i].range = m_LightData.spotLights[i].range;
        cbData.spotLights[i].dirX = m_LightData.spotLights[i].dirX;
        cbData.spotLights[i].dirY = m_LightData.spotLights[i].dirY;
        cbData.spotLights[i].dirZ = m_LightData.spotLights[i].dirZ;
        cbData.spotLights[i].innerCos = m_LightData.spotLights[i].innerCos;
        cbData.spotLights[i].colorR = m_LightData.spotLights[i].colorR;
        cbData.spotLights[i].colorG = m_LightData.spotLights[i].colorG;
        cbData.spotLights[i].colorB = m_LightData.spotLights[i].colorB;
        cbData.spotLights[i].outerCos = m_LightData.spotLights[i].outerCos;
        cbData.spotLights[i].intensity = m_LightData.spotLights[i].intensity;
        cbData.spotLights[i].shadowEnabled = m_LightData.spotLights[i].shadowEnabled;
        cbData.spotLights[i].shadowBaseSlice = m_LightData.spotLights[i].shadowBaseSlice;
        cbData.spotLights[i].shadowBias = m_LightData.spotLights[i].shadowBias;
    }

    // Material properties
    cbData.materialColor[0] = m_MaterialData.colorR;
    cbData.materialColor[1] = m_MaterialData.colorG;
    cbData.materialColor[2] = m_MaterialData.colorB;
    cbData.materialMetallic = m_MaterialData.metallic;
    cbData.materialRoughness = m_MaterialData.roughness;
    cbData.materialAmbientOcclusion = m_MaterialData.ambientOcclusion;
    cbData.materialEmissiveColor[0] = m_MaterialData.emissiveColorR;
    cbData.materialEmissiveColor[1] = m_MaterialData.emissiveColorG;
    cbData.materialEmissiveColor[2] = m_MaterialData.emissiveColorB;
    cbData.materialEmissiveStrength = m_MaterialData.emissiveStrength;

    for (int i = 0; i < 4; ++i)
        cbData.hasTextures[i] = 0.0f;
    cbData.albedoTextureSlot = -1.0f;
    cbData.normalTextureSlot = -1.0f;
    cbData.ormTextureSlot = -1.0f;

    if (mesh.lodHasTexturedSubmeshes[0])
    {
        cbData.hasTextures[0] = 1.0f;
        cbData.albedoTextureSlot = 0.0f;
    }

    cbData.uvTiling[0] = m_MaterialData.tilingU;
    cbData.uvTiling[1] = m_MaterialData.tilingV;
    cbData.uvOffset[0] = m_MaterialData.offsetU;
    cbData.uvOffset[1] = m_MaterialData.offsetV;
    cbData.lightmapUvScale[0] = m_MaterialData.lightmapScaleU;
    cbData.lightmapUvScale[1] = m_MaterialData.lightmapScaleV;
    cbData.lightmapUvOffset[0] = m_MaterialData.lightmapOffsetU;
    cbData.lightmapUvOffset[1] = m_MaterialData.lightmapOffsetV;
    cbData.lightmapEnabled = 0.0f;
    cbData.lightmapTextureSlot = -1.0f;
    cbData.lightmapIntensity = m_MaterialData.lightmapIntensity;
    for (uint32_t probeIndex = 0; probeIndex < kMaxReflectionProbeBlendCount; ++probeIndex)
    {
        cbData.reflectionProbePositionRadius[probeIndex][0] = m_ReflectionProbeData[probeIndex].position.x;
        cbData.reflectionProbePositionRadius[probeIndex][1] = m_ReflectionProbeData[probeIndex].position.y;
        cbData.reflectionProbePositionRadius[probeIndex][2] = m_ReflectionProbeData[probeIndex].position.z;
        cbData.reflectionProbePositionRadius[probeIndex][3] = m_ReflectionProbeData[probeIndex].radius;
        cbData.reflectionProbeTintIntensity[probeIndex][0] = m_ReflectionProbeData[probeIndex].tint.x;
        cbData.reflectionProbeTintIntensity[probeIndex][1] = m_ReflectionProbeData[probeIndex].tint.y;
        cbData.reflectionProbeTintIntensity[probeIndex][2] = m_ReflectionProbeData[probeIndex].tint.z;
        cbData.reflectionProbeTintIntensity[probeIndex][3] = m_ReflectionProbeData[probeIndex].intensity;
        cbData.reflectionProbeParams[probeIndex][0] = m_ReflectionProbeData[probeIndex].falloff;
        cbData.reflectionProbeParams[probeIndex][1] = m_ReflectionProbeData[probeIndex].rotation;
        cbData.reflectionProbeParams[probeIndex][2] = m_ReflectionProbeData[probeIndex].blendWeight;
        cbData.reflectionProbeParams[probeIndex][3] = m_ReflectionProbeData[probeIndex].enabled ? 1.0f : 0.0f;
        cbData.reflectionProbeBoxExtents[probeIndex][0] = m_ReflectionProbeData[probeIndex].boxExtents.x;
        cbData.reflectionProbeBoxExtents[probeIndex][1] = m_ReflectionProbeData[probeIndex].boxExtents.y;
        cbData.reflectionProbeBoxExtents[probeIndex][2] = m_ReflectionProbeData[probeIndex].boxExtents.z;
        cbData.reflectionProbeBoxExtents[probeIndex][3] = 0.0f;
    }

    // Bind textures if material has them
    bool anyTexture = false;
    TextureCacheEntry* firstTex = nullptr;

    // Use colormap from FBX if no material texture is set
    std::string texturePaths[4];
    for (int i = 0; i < 4; ++i)
        texturePaths[i] = m_MaterialData.texturePaths[i];
    if (texturePaths[0].empty() && !mesh.colormapPath.empty())
        texturePaths[0] = mesh.colormapPath;

    int albedoTextureSlot = m_MaterialData.albedoTextureSlot;
    int normalTextureSlot = m_MaterialData.normalTextureSlot;
    int ormTextureSlot = m_MaterialData.ormTextureSlot;
    if (albedoTextureSlot < 0 && mesh.lodHasTexturedSubmeshes[0])
        albedoTextureSlot = 0;
    if (albedoTextureSlot < 0 && !texturePaths[0].empty() &&
        m_MaterialData.textureSampleTypes[0] == static_cast<int>(TextureSampleType::Color))
        albedoTextureSlot = 0;
    if (normalTextureSlot < 0)
    {
        for (int i = 0; i < 4; ++i)
        {
            if (!texturePaths[i].empty() &&
                m_MaterialData.textureSampleTypes[i] == static_cast<int>(TextureSampleType::Normal))
            {
                normalTextureSlot = i;
                break;
            }
        }
    }
    if (ormTextureSlot < 0)
    {
        for (int i = 0; i < 4; ++i)
        {
            if (!texturePaths[i].empty() &&
                m_MaterialData.textureSampleTypes[i] == static_cast<int>(TextureSampleType::Mask))
            {
                ormTextureSlot = i;
                break;
            }
        }
    }

    if (m_MaterialData.lightmapEnabled && !m_MaterialData.lightmapTexturePath.empty())
    {
        for (int slot = 1; slot < 4; ++slot)
        {
            if (!texturePaths[slot].empty())
                continue;

            texturePaths[slot] = m_MaterialData.lightmapTexturePath;
            cbData.lightmapEnabled = 1.0f;
            cbData.lightmapTextureSlot = static_cast<float>(slot);
            break;
        }
    }

    for (int i = 0; i < 4; ++i)
    {
        if (!texturePaths[i].empty())
        {
            anyTexture = true;
            TextureCacheEntry* tex =
                LoadMaterialTexture(texturePaths[i], ResolveTextureSemanticForMaterialSlot(texturePaths[i], m_MaterialData.textureSampleTypes[i]));
            if (tex && tex->handle.IsReady())
            {
                if (!firstTex)
                    firstTex = tex;
            }
        }
    }

    if (anyTexture && firstTex && firstTex->handle.IsReady())
    {
        // Use a descriptor heap for this draw call's textures and shared scene SRVs.
        if (!firstTex->srvHeap)
        {
            ID3D12Device* d3dDevice = GetD3D12DevicePtr(m_Device);
            D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
            srvHeapDesc.NumDescriptors = 12; // t0-t3 material, t4-t6 shadows/AO, t7-t9 Forward+, t10-t11 probes
            srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
            srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

            ID3D12DescriptorHeap* srvHeap = nullptr;
            if (SUCCEEDED(d3dDevice->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&srvHeap))))
            {
                firstTex->srvHeap = srvHeap;
            }
        }

        if (firstTex->srvHeap)
        {
            ID3D12DescriptorHeap* materialHeap = static_cast<ID3D12DescriptorHeap*>(firstTex->srvHeap);
            ID3D12Device* d3dDevice = GetD3D12DevicePtr(m_Device);
            UINT srvSize = d3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
            std::string descriptorSignature;
            descriptorSignature.reserve(128);
            for (int i = 0; i < 4; ++i)
            {
                descriptorSignature += BuildTextureSemanticCacheKey(
                    texturePaths[i], ResolveTextureSemanticForMaterialSlot(texturePaths[i], m_MaterialData.textureSampleTypes[i]));
                descriptorSignature += "|";
            }
            descriptorSignature += std::to_string(reinterpret_cast<uintptr_t>(m_ShadowSRVHeap));
            descriptorSignature += "|";
            descriptorSignature += std::to_string(reinterpret_cast<uintptr_t>(m_SSAOSRVHeap));
            descriptorSignature += "|";
            descriptorSignature += std::to_string(reinterpret_cast<uintptr_t>(m_LightGridBuffer));
            descriptorSignature += "|";
            descriptorSignature += std::to_string(reinterpret_cast<uintptr_t>(m_LightIndexBuffer));
            descriptorSignature += "|";
            descriptorSignature += std::to_string(m_TileCountX);
            descriptorSignature += "|";
            descriptorSignature += std::to_string(m_TileCountY);
            const bool refreshDescriptors = (firstTex->descriptorSignature != descriptorSignature);

            for (int i = 0; i < 4; ++i)
            {
                if (!texturePaths[i].empty())
                {
                    TextureCacheEntry* tex = LoadMaterialTexture(
                        texturePaths[i], ResolveTextureSemanticForMaterialSlot(texturePaths[i], m_MaterialData.textureSampleTypes[i]));
                    if (tex && tex->handle.IsReady())
                    {
                        cbData.hasTextures[i] = 1.0f;
                        if (refreshDescriptors)
                        {
                            RHITexturePtr texture = tex->handle->GetTexture();
                            if (texture)
                            {
                                void* native = m_Device->GetNativeTextureResource(texture.get());
                                ID3D12Resource* d3dResource = static_cast<ID3D12Resource*>(native);

                                D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
                                srvDesc.Format = GetTextureSrvFormat(texture->GetFormat());
                                srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
                                srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
                                srvDesc.Texture2D.MipLevels = texture->GetMipLevels();
                                srvDesc.Texture2D.MostDetailedMip = 0;

                                D3D12_CPU_DESCRIPTOR_HANDLE dest = materialHeap->GetCPUDescriptorHandleForHeapStart();
                                dest.ptr += i * srvSize;
                                d3dDevice->CreateShaderResourceView(d3dResource, &srvDesc, dest);
                            }
                        }
                    }
                }
            }

            // Bind constant buffer (Index 0 is valid for both root signatures)
            ID3D12Resource* cbRes = static_cast<ID3D12Resource*>(m_CBResource);
            if (cbRes)
                cmdList->SetGraphicsRootConstantBufferView(0, cbRes->GetGPUVirtualAddress() + m_CurrentCBOffset);

            // Only bind material textures if NOT in depth prepass
            // Depth prepass uses shadow PSO which only has root param 0 (CBV)
            if (!m_IsDepthPrepass)
            {
                WriteReflectionProbeDescriptors(materialHeap, 10);

                // Set descriptor heaps before binding descriptor tables
                ID3D12DescriptorHeap* samplerHeap = static_cast<ID3D12DescriptorHeap*>(m_SamplerHeap);
                ID3D12DescriptorHeap* heaps[] = {materialHeap, samplerHeap};
                cmdList->SetDescriptorHeaps(samplerHeap ? 2 : 1, heaps);

                // Root param 1: material SRVs (t0-t3)
                cmdList->SetGraphicsRootDescriptorTable(1, materialHeap->GetGPUDescriptorHandleForHeapStart());
                // Root param 2: material samplers (s0-s3)
                if (samplerHeap)
                    cmdList->SetGraphicsRootDescriptorTable(2, samplerHeap->GetGPUDescriptorHandleForHeapStart());

                // Bind shadow map to slot 4
                if (refreshDescriptors && m_ShadowSRVHeap)
                {
                    ID3D12DescriptorHeap* shadowHeap = static_cast<ID3D12DescriptorHeap*>(m_ShadowSRVHeap);
                    D3D12_CPU_DESCRIPTOR_HANDLE shadowSrc = shadowHeap->GetCPUDescriptorHandleForHeapStart();
                    shadowSrc.ptr += 4 * srvSize;
                    D3D12_CPU_DESCRIPTOR_HANDLE shadowDest = materialHeap->GetCPUDescriptorHandleForHeapStart();
                    shadowDest.ptr += 4 * srvSize;
                    d3dDevice->CopyDescriptorsSimple(1, shadowDest, shadowSrc, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
                }

                // Bind SSAO Blurred texture to slot 5
                if (refreshDescriptors && m_SSAOSRVHeap)
                {
                    ID3D12DescriptorHeap* ssaoHeap = static_cast<ID3D12DescriptorHeap*>(m_SSAOSRVHeap);
                    D3D12_CPU_DESCRIPTOR_HANDLE ssaoSrc = ssaoHeap->GetCPUDescriptorHandleForHeapStart();
                    ssaoSrc.ptr += 3 * srvSize;
                    D3D12_CPU_DESCRIPTOR_HANDLE ssaoDest = materialHeap->GetCPUDescriptorHandleForHeapStart();
                    ssaoDest.ptr += 5 * srvSize;
                    d3dDevice->CopyDescriptorsSimple(1, ssaoDest, ssaoSrc, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
                }
                else if (refreshDescriptors)
                {
                    D3D12_SHADER_RESOURCE_VIEW_DESC nullDesc = {};
                    nullDesc.Format = DXGI_FORMAT_R8_UNORM;
                    nullDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
                    nullDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
                    nullDesc.Texture2D.MipLevels = 1;
                    D3D12_CPU_DESCRIPTOR_HANDLE ssaoDest = materialHeap->GetCPUDescriptorHandleForHeapStart();
                    ssaoDest.ptr += 5 * srvSize;
                    d3dDevice->CreateShaderResourceView(nullptr, &nullDesc, ssaoDest);
                }

                // Bind local shadow array to slot 6.
                if (refreshDescriptors && m_ShadowSRVHeap)
                {
                    ID3D12DescriptorHeap* shadowHeap = static_cast<ID3D12DescriptorHeap*>(m_ShadowSRVHeap);
                    D3D12_CPU_DESCRIPTOR_HANDLE localShadowSrc = shadowHeap->GetCPUDescriptorHandleForHeapStart();
                    localShadowSrc.ptr += 6 * srvSize;
                    D3D12_CPU_DESCRIPTOR_HANDLE localShadowDest = materialHeap->GetCPUDescriptorHandleForHeapStart();
                    localShadowDest.ptr += 6 * srvSize;
                    d3dDevice->CopyDescriptorsSimple(1, localShadowDest, localShadowSrc,
                                                     D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
                }

                // Bind Forward+ buffers to slots 7, 8, 9
                if (refreshDescriptors && m_ForwardPlusSRVHeap && m_ForwardPlusEnabled)
                {
                    ID3D12DescriptorHeap* fPlusHeap = static_cast<ID3D12DescriptorHeap*>(m_ForwardPlusSRVHeap);
                    D3D12_CPU_DESCRIPTOR_HANDLE fPlusDest = materialHeap->GetCPUDescriptorHandleForHeapStart();
                    fPlusDest.ptr += 7 * srvSize;

                    // Copy AllLights SRV (Index 1 in fPlusHeap)
                    D3D12_CPU_DESCRIPTOR_HANDLE lightSrc = fPlusHeap->GetCPUDescriptorHandleForHeapStart();
                    lightSrc.ptr += 1 * srvSize;
                    d3dDevice->CopyDescriptorsSimple(1, fPlusDest, lightSrc, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

                    // Create/Copy Grid SRV (currently we only have UAV in fPlusHeap, let's create SRV here or fix
                    // fPlusHeap) For now, let's create SRVs onto the material heap
                    fPlusDest.ptr += 1 * srvSize; // Move to slot 8
                    D3D12_SHADER_RESOURCE_VIEW_DESC gridSrvDesc = {};
                    gridSrvDesc.Format = DXGI_FORMAT_R32_UINT;
                    gridSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
                    gridSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
                    gridSrvDesc.Buffer.NumElements = m_TileCountX * m_TileCountY * 2;
                    d3dDevice->CreateShaderResourceView(reinterpret_cast<ID3D12Resource*>(m_LightGridBuffer),
                                                        &gridSrvDesc, fPlusDest);

                    fPlusDest.ptr += 1 * srvSize; // Move to slot 9
                    D3D12_SHADER_RESOURCE_VIEW_DESC indexSrvDesc = {};
                    indexSrvDesc.Format = DXGI_FORMAT_R32_UINT;
                    indexSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
                    indexSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
                    indexSrvDesc.Buffer.NumElements = m_TileCountX * m_TileCountY * MAX_LIGHTS_PER_TILE;
                    d3dDevice->CreateShaderResourceView(reinterpret_cast<ID3D12Resource*>(m_LightIndexBuffer),
                                                        &indexSrvDesc, fPlusDest);
                }

                // Bind Shadow/SSAO/Forward+ Table (Index 3)
                D3D12_GPU_DESCRIPTOR_HANDLE shadowAOGpuHandle = materialHeap->GetGPUDescriptorHandleForHeapStart();
                shadowAOGpuHandle.ptr += 4 * srvSize;
                cmdList->SetGraphicsRootDescriptorTable(3, shadowAOGpuHandle);

                // Bind Forward+ SRV table (Index 4) - t7=AllLights, t8=LightGrid, t9=LightIndices
                D3D12_GPU_DESCRIPTOR_HANDLE forwardPlusGpuHandle = materialHeap->GetGPUDescriptorHandleForHeapStart();
                forwardPlusGpuHandle.ptr += 7 * srvSize; // Start at slot 7
                cmdList->SetGraphicsRootDescriptorTable(4, forwardPlusGpuHandle);
            }

            if (refreshDescriptors)
            {
                firstTex->descriptorSignature = descriptorSignature;
                firstTex->descriptorUpdateFrame = m_FrameCounter;
            }
        }
    }

    cbData.viewportWidth = (float)swapChain->GetWidth();
    cbData.viewportHeight = (float)swapChain->GetHeight();
    cbData.fPlusEnabled = m_ForwardPlusEnabled ? 1.0f : 0.0f;
    cbData.tileCountX = m_TileCountX;
    cbData.tileCountY = m_TileCountY;
    cbData.debugVisMode = static_cast<uint32_t>(viewSettings.debugVisMode);
    cbData.ssaoEnabled = (m_SSAOSettings.enabled && !m_SceneCaptureMode && m_SSAOSRVHeap && m_SSAOBlurredRT) ? 1.0f : 0.0f;

    // Camera and Animation
    float camX, camY, camZ;
    const_cast<Camera&>(camera).GetPosition(camX, camY, camZ);
    cbData.cameraPos[0] = camX;
    cbData.cameraPos[1] = camY;
    cbData.cameraPos[2] = camZ;
    cbData.time = m_ElapsedTime;
    cbData.pannerSpeed[0] = m_MaterialData.pannerSpeedU;
    cbData.pannerSpeed[1] = m_MaterialData.pannerSpeedV;
    cbData.pannerMethod = m_MaterialData.pannerMethod;
    cbData.pannerLink = m_MaterialData.pannerLink ? 1 : 0;

    uint32_t debugLodLevel = 0;
    uint32_t renderLodLevel = 0;
    const bool showLodVisualization =
        viewSettings.debugVisMode == DebugVisMode::LODVisualization || viewSettings.lodDebugTint;
    const bool canUseAnyLod = HasGeneratedMeshLod(mesh, 1) || HasGeneratedMeshLod(mesh, 2);
    if (!m_IsDepthPrepass && (showLodVisualization || canUseAnyLod))
    {
        debugLodLevel = ChooseMeshLodLevel(camera, mesh, worldMatrix);
        if (canUseAnyLod)
            renderLodLevel = debugLodLevel;
    }
    SetLodDebugTint(cbData.lodDebugTint, showLodVisualization, debugLodLevel);

    // Shadow parameters
    memcpy(cbData.shadowMatrix, m_ShadowLightMatrix, sizeof(m_ShadowLightMatrix));
    cbData.shadowBias = m_ShadowBias;
    cbData.shadowEnabled = m_ShadowEnabled ? 1.0f : 0.0f;

    // Map and copy
    if (m_MappedLightBuffer)
    {
        uint8_t* targetPtr = static_cast<uint8_t*>(m_MappedLightBuffer) + m_CurrentCBOffset;
        memcpy(targetPtr, &cbData, sizeof(ConstantBufferData));

        ID3D12Resource* cbRes = static_cast<ID3D12Resource*>(m_CBResource);
        if (cbRes)
        {
            cmdList->SetGraphicsRootConstantBufferView(0, cbRes->GetGPUVirtualAddress() + m_CurrentCBOffset);
        }

        m_CurrentCBOffset += alignedSize;
    }

    if (!mesh.vertexBuffer || !mesh.indexBuffer || !GetD3D12Buffer(mesh.vertexBuffer.get()) ||
        !GetD3D12Buffer(mesh.indexBuffer.get()))
    {
        return;
    }

    RHIBufferPtr selectedVertexBuffer =
        (renderLodLevel < PrimitiveMesh::kLodCount && mesh.lodVertexBuffers[renderLodLevel] &&
         HasGeneratedMeshLod(mesh, renderLodLevel))
            ? mesh.lodVertexBuffers[renderLodLevel]
            : mesh.vertexBuffer;
    RHIBufferPtr selectedIndexBuffer =
        (renderLodLevel < PrimitiveMesh::kLodCount && mesh.lodIndexBuffers[renderLodLevel] &&
         HasGeneratedMeshLod(mesh, renderLodLevel))
            ? mesh.lodIndexBuffers[renderLodLevel]
            : mesh.indexBuffer;
    uint32_t selectedIndexCount =
        (renderLodLevel < PrimitiveMesh::kLodCount && mesh.lodIndexCounts[renderLodLevel] > 0 &&
         HasGeneratedMeshLod(mesh, renderLodLevel))
            ? mesh.lodIndexCounts[renderLodLevel]
            : mesh.indexCount;
    const std::vector<Submesh>& drawSubmeshes =
        (renderLodLevel < PrimitiveMesh::kLodCount && HasGeneratedMeshLod(mesh, renderLodLevel) &&
         !mesh.lodSubmeshes[renderLodLevel].empty())
            ? mesh.lodSubmeshes[renderLodLevel]
            : mesh.submeshes;
    const std::vector<PrimitiveMesh::RenderBatch>& drawBatches =
        (renderLodLevel < PrimitiveMesh::kLodCount && HasGeneratedMeshLod(mesh, renderLodLevel) &&
         !mesh.lodRenderBatches[renderLodLevel].empty())
            ? mesh.lodRenderBatches[renderLodLevel]
            : mesh.lodRenderBatches[0];

    if (!selectedVertexBuffer || !selectedIndexBuffer || !GetD3D12Buffer(selectedVertexBuffer.get()) ||
        !GetD3D12Buffer(selectedIndexBuffer.get()))
        return;

    D3D12_VERTEX_BUFFER_VIEW vbView = {};
    vbView.BufferLocation = GetD3D12BufferGPUAddress(selectedVertexBuffer.get());
    vbView.SizeInBytes = static_cast<UINT>(selectedVertexBuffer->GetSize());
    vbView.StrideInBytes = sizeof(PrimitiveVertex);
    cmdList->IASetVertexBuffers(0, 1, &vbView);

    D3D12_INDEX_BUFFER_VIEW ibView = {};
    ibView.BufferLocation = GetD3D12BufferGPUAddress(selectedIndexBuffer.get());
    ibView.SizeInBytes = static_cast<UINT>(selectedIndexBuffer->GetSize());
    ibView.Format = DXGI_FORMAT_R32_UINT;
    cmdList->IASetIndexBuffer(&ibView);

    cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    // Always bind shadow SRV to root param 3 if not already bound via textured path
    bool anyTextureBound = false;
    for (int i = 0; i < 4; ++i)
    {
        if (cbData.hasTextures[i] > 0.5f)
        {
            anyTextureBound = true;
            break;
        }
    }

    // If no texture was bound (or texture path didn't bind shadow), bind shadow + SSAO SRV now
    // Skip this for depth prepass as shadow root signature doesn't have these params
    if (!anyTextureBound && !m_IsDepthPrepass)
    {
        ID3D12DescriptorHeap* shadowSrvHeap = static_cast<ID3D12DescriptorHeap*>(m_ShadowSRVHeap);
        ID3D12DescriptorHeap* ssaoSrvHeap = static_cast<ID3D12DescriptorHeap*>(m_SSAOSRVHeap);
        ID3D12DescriptorHeap* samplerHeap = static_cast<ID3D12DescriptorHeap*>(m_SamplerHeap);

        ID3D12Device* d3dDevice = GetD3D12DevicePtr(m_Device);
        UINT srvSize = d3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

        if (shadowSrvHeap)
        {
            if (ssaoSrvHeap && m_SharedDescriptorFrame != m_FrameCounter)
            {
                D3D12_CPU_DESCRIPTOR_HANDLE ssaoSrc = ssaoSrvHeap->GetCPUDescriptorHandleForHeapStart();
                ssaoSrc.ptr += 3 * srvSize;
                D3D12_CPU_DESCRIPTOR_HANDLE ssaoDest = shadowSrvHeap->GetCPUDescriptorHandleForHeapStart();
                ssaoDest.ptr += 5 * srvSize;
                d3dDevice->CopyDescriptorsSimple(1, ssaoDest, ssaoSrc, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
                m_SharedDescriptorFrame = m_FrameCounter;
            }
            else if (!ssaoSrvHeap && m_SharedDescriptorFrame != m_FrameCounter)
            {
                D3D12_SHADER_RESOURCE_VIEW_DESC nullDesc = {};
                nullDesc.Format = DXGI_FORMAT_R8_UNORM;
                nullDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
                nullDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
                nullDesc.Texture2D.MipLevels = 1;
                D3D12_CPU_DESCRIPTOR_HANDLE ssaoDest = shadowSrvHeap->GetCPUDescriptorHandleForHeapStart();
                ssaoDest.ptr += 5 * srvSize;
                d3dDevice->CreateShaderResourceView(nullptr, &nullDesc, ssaoDest);
                m_SharedDescriptorFrame = m_FrameCounter;
            }

            ID3D12DescriptorHeap* heaps[] = {shadowSrvHeap, samplerHeap};
            cmdList->SetDescriptorHeaps(2, heaps);

            WriteReflectionProbeDescriptors(shadowSrvHeap, 10);

            D3D12_GPU_DESCRIPTOR_HANDLE shadowAOGpuHandle = shadowSrvHeap->GetGPUDescriptorHandleForHeapStart();
            shadowAOGpuHandle.ptr += 4 * srvSize;
            cmdList->SetGraphicsRootDescriptorTable(3, shadowAOGpuHandle);

            D3D12_GPU_DESCRIPTOR_HANDLE forwardPlusGpuHandle = shadowSrvHeap->GetGPUDescriptorHandleForHeapStart();
            forwardPlusGpuHandle.ptr += 7 * srvSize;
            cmdList->SetGraphicsRootDescriptorTable(4, forwardPlusGpuHandle);
        }
    }

    // Draw - either per-submesh (multi-material) or whole mesh (single material)
    const bool singleSpanSubmesh = (drawSubmeshes.size() == 1 && drawSubmeshes[0].indexStart == 0 &&
                                    drawSubmeshes[0].indexCount == selectedIndexCount);
    if (!drawSubmeshes.empty() && !singleSpanSubmesh)
    {
        ID3D12Device* d3dDevice = GetD3D12DevicePtr(m_Device);
        if (!d3dDevice)
        {
            // Can't render submeshes without device
            cmdList->DrawIndexedInstanced(mesh.indexCount, 1, 0, 0, 0);
            return;
        }

        // Multi-material: bind once per material batch, then draw all of its sections.
        for (const PrimitiveMesh::RenderBatch& batch : drawBatches)
        {
            if (batch.sections.empty())
                continue;

            if (d3dDevice && m_Device && !batch.texturePath.empty())
            {
                const int lightmapSlot =
                    (cbData.lightmapEnabled > 0.5f && cbData.lightmapTextureSlot >= 1.0f)
                        ? static_cast<int>(cbData.lightmapTextureSlot)
                        : -1;

                // DEBUG LOGGING
                // std::printf("Submesh %s: Path '%s'\n", mesh.name.c_str(), submesh.texturePath.c_str());

                TextureCacheEntry* tex = LoadMaterialTexture(batch.texturePath, TextureSemantic::Color);
                if (tex && tex->handle.IsReady())
                {
                    RHITexturePtr texture = tex->handle->GetTexture();
                    if (texture && texture.get())
                    {
                        void* native = m_Device->GetNativeTextureResource(texture.get());
                        if (native)
                        {
                            ID3D12Resource* d3dResource = static_cast<ID3D12Resource*>(native);

                            if (!tex->srvHeap)
                            {
                                D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
                                srvHeapDesc.NumDescriptors = 12;
                                srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
                                srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
                                ID3D12DescriptorHeap* srvHeap = nullptr;
                                if (SUCCEEDED(d3dDevice->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&srvHeap))))
                                    tex->srvHeap = srvHeap;
                            }

                            if (tex->srvHeap)
                            {
                                ID3D12DescriptorHeap* submeshHeap = static_cast<ID3D12DescriptorHeap*>(tex->srvHeap);
                                UINT srvSize =
                                    d3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
                                std::string descriptorSignature;
                                descriptorSignature.reserve(128);
                                descriptorSignature += BuildTextureSemanticCacheKey(batch.texturePath, TextureSemantic::Color);
                                descriptorSignature += "|";
                                if (lightmapSlot >= 1 && lightmapSlot < 4)
                                {
                                    descriptorSignature += BuildTextureSemanticCacheKey(texturePaths[lightmapSlot], TextureSemantic::Data);
                                }
                                descriptorSignature += "|";
                                descriptorSignature += std::to_string(reinterpret_cast<uintptr_t>(d3dResource));
                                descriptorSignature += "|";
                                descriptorSignature += std::to_string(texture->GetMipLevels());
                                descriptorSignature += "|";
                                descriptorSignature += std::to_string(reinterpret_cast<uintptr_t>(m_ShadowSRVHeap));
                                descriptorSignature += "|";
                                descriptorSignature += std::to_string(reinterpret_cast<uintptr_t>(m_SSAOSRVHeap));
                                descriptorSignature += "|";
                                descriptorSignature += std::to_string(reinterpret_cast<uintptr_t>(m_LightGridBuffer));
                                descriptorSignature += "|";
                                descriptorSignature += std::to_string(reinterpret_cast<uintptr_t>(m_LightIndexBuffer));
                                descriptorSignature += "|";
                                descriptorSignature += std::to_string(m_ForwardPlusEnabled ? 1 : 0);
                                descriptorSignature += "|";
                                descriptorSignature += std::to_string(m_TileCountX);
                                descriptorSignature += "|";
                                descriptorSignature += std::to_string(m_TileCountY);
                                const bool refreshDescriptors = (tex->descriptorSignature != descriptorSignature);

                                if (refreshDescriptors)
                                {
                                    // Slot 0: material texture.
                                    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
                                    srvDesc.Format = GetTextureSrvFormat(texture->GetFormat());
                                    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
                                    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
                                    srvDesc.Texture2D.MipLevels = texture->GetMipLevels();
                                    srvDesc.Texture2D.MostDetailedMip = 0;
                                    D3D12_CPU_DESCRIPTOR_HANDLE dest = submeshHeap->GetCPUDescriptorHandleForHeapStart();
                                    d3dDevice->CreateShaderResourceView(d3dResource, &srvDesc, dest);

                                    if (lightmapSlot >= 1 && lightmapSlot < 4 && !texturePaths[lightmapSlot].empty())
                                    {
                                        TextureCacheEntry* lightmapTex =
                                            LoadMaterialTexture(texturePaths[lightmapSlot], TextureSemantic::Data);
                                        if (lightmapTex && lightmapTex->handle.IsReady())
                                        {
                                            RHITexturePtr lightmapTexture = lightmapTex->handle->GetTexture();
                                            if (lightmapTexture)
                                            {
                                                void* lightmapNative =
                                                    m_Device->GetNativeTextureResource(lightmapTexture.get());
                                                ID3D12Resource* lightmapResource =
                                                    static_cast<ID3D12Resource*>(lightmapNative);
                                                if (lightmapResource)
                                                {
                                                    D3D12_SHADER_RESOURCE_VIEW_DESC lightmapSrvDesc = {};
                                                    lightmapSrvDesc.Format = GetTextureSrvFormat(lightmapTexture->GetFormat());
                                                    lightmapSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
                                                    lightmapSrvDesc.Shader4ComponentMapping =
                                                        D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
                                                    lightmapSrvDesc.Texture2D.MipLevels = lightmapTexture->GetMipLevels();
                                                    lightmapSrvDesc.Texture2D.MostDetailedMip = 0;
                                                    D3D12_CPU_DESCRIPTOR_HANDLE lightmapDest =
                                                        submeshHeap->GetCPUDescriptorHandleForHeapStart();
                                                    lightmapDest.ptr += lightmapSlot * srvSize;
                                                    d3dDevice->CreateShaderResourceView(lightmapResource,
                                                                                        &lightmapSrvDesc, lightmapDest);
                                                }
                                            }
                                        }
                                    }

                                    // Slot 4: shadow map.
                                    D3D12_CPU_DESCRIPTOR_HANDLE shadowDest =
                                        submeshHeap->GetCPUDescriptorHandleForHeapStart();
                                    shadowDest.ptr += 4 * srvSize;
                                    if (m_ShadowSRVHeap)
                                    {
                                        ID3D12DescriptorHeap* shadowHeap =
                                            static_cast<ID3D12DescriptorHeap*>(m_ShadowSRVHeap);
                                        D3D12_CPU_DESCRIPTOR_HANDLE shadowSrc =
                                            shadowHeap->GetCPUDescriptorHandleForHeapStart();
                                        shadowSrc.ptr += 4 * srvSize;
                                        d3dDevice->CopyDescriptorsSimple(1, shadowDest, shadowSrc,
                                                                         D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
                                    }
                                    else
                                    {
                                        D3D12_SHADER_RESOURCE_VIEW_DESC nullDesc = {};
                                        nullDesc.Format = DXGI_FORMAT_R32_FLOAT;
                                        nullDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
                                        nullDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
                                        nullDesc.Texture2D.MipLevels = 1;
                                        d3dDevice->CreateShaderResourceView(nullptr, &nullDesc, shadowDest);
                                    }

                                    // Slot 5: SSAO.
                                    D3D12_CPU_DESCRIPTOR_HANDLE ssaoDest = submeshHeap->GetCPUDescriptorHandleForHeapStart();
                                    ssaoDest.ptr += 5 * srvSize;
                                    if (m_SSAOSRVHeap)
                                    {
                                        ID3D12DescriptorHeap* ssaoHeap =
                                            static_cast<ID3D12DescriptorHeap*>(m_SSAOSRVHeap);
                                        D3D12_CPU_DESCRIPTOR_HANDLE ssaoSrc =
                                            ssaoHeap->GetCPUDescriptorHandleForHeapStart();
                                        ssaoSrc.ptr += 3 * srvSize;
                                        d3dDevice->CopyDescriptorsSimple(1, ssaoDest, ssaoSrc,
                                                                         D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
                                    }
                                    else
                                    {
                                        D3D12_SHADER_RESOURCE_VIEW_DESC nullDesc = {};
                                        nullDesc.Format = DXGI_FORMAT_R8_UNORM;
                                        nullDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
                                        nullDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
                                        nullDesc.Texture2D.MipLevels = 1;
                                        d3dDevice->CreateShaderResourceView(nullptr, &nullDesc, ssaoDest);
                                    }

                                    // Slot 6: local shadow array.
                                    D3D12_CPU_DESCRIPTOR_HANDLE localShadowDest =
                                        submeshHeap->GetCPUDescriptorHandleForHeapStart();
                                    localShadowDest.ptr += 6 * srvSize;
                                    if (m_ShadowSRVHeap)
                                    {
                                        ID3D12DescriptorHeap* shadowHeap =
                                            static_cast<ID3D12DescriptorHeap*>(m_ShadowSRVHeap);
                                        D3D12_CPU_DESCRIPTOR_HANDLE localShadowSrc =
                                            shadowHeap->GetCPUDescriptorHandleForHeapStart();
                                        localShadowSrc.ptr += 6 * srvSize;
                                        d3dDevice->CopyDescriptorsSimple(1, localShadowDest, localShadowSrc,
                                                                         D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
                                    }
                                    else
                                    {
                                        D3D12_SHADER_RESOURCE_VIEW_DESC nullLocalDesc = {};
                                        nullLocalDesc.Format = DXGI_FORMAT_R32_FLOAT;
                                        nullLocalDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
                                        nullLocalDesc.Shader4ComponentMapping =
                                            D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
                                        nullLocalDesc.Texture2DArray.MipLevels = 1;
                                        nullLocalDesc.Texture2DArray.ArraySize = 1;
                                        d3dDevice->CreateShaderResourceView(nullptr, &nullLocalDesc, localShadowDest);
                                    }

                                    // Slots 7-9: Forward+ resources.
                                    D3D12_CPU_DESCRIPTOR_HANDLE fPlusDest6 =
                                        submeshHeap->GetCPUDescriptorHandleForHeapStart();
                                    fPlusDest6.ptr += 7 * srvSize;
                                    D3D12_CPU_DESCRIPTOR_HANDLE fPlusDest7 =
                                        submeshHeap->GetCPUDescriptorHandleForHeapStart();
                                    fPlusDest7.ptr += 8 * srvSize;
                                    D3D12_CPU_DESCRIPTOR_HANDLE fPlusDest8 =
                                        submeshHeap->GetCPUDescriptorHandleForHeapStart();
                                    fPlusDest8.ptr += 9 * srvSize;

                                    if (m_ForwardPlusSRVHeap && m_ForwardPlusEnabled && m_LightGridBuffer &&
                                        m_LightIndexBuffer)
                                    {
                                        ID3D12DescriptorHeap* fPlusHeap =
                                            static_cast<ID3D12DescriptorHeap*>(m_ForwardPlusSRVHeap);
                                        D3D12_CPU_DESCRIPTOR_HANDLE lightSrc =
                                            fPlusHeap->GetCPUDescriptorHandleForHeapStart();
                                        lightSrc.ptr += 1 * srvSize;
                                        d3dDevice->CopyDescriptorsSimple(1, fPlusDest6, lightSrc,
                                                                         D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

                                        D3D12_SHADER_RESOURCE_VIEW_DESC gridSrvDesc = {};
                                        gridSrvDesc.Format = DXGI_FORMAT_R32_UINT;
                                        gridSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
                                        gridSrvDesc.Shader4ComponentMapping =
                                            D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
                                        gridSrvDesc.Buffer.NumElements = m_TileCountX * m_TileCountY * 2;
                                        gridSrvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
                                        d3dDevice->CreateShaderResourceView(
                                            static_cast<ID3D12Resource*>(m_LightGridBuffer), &gridSrvDesc, fPlusDest7);

                                        D3D12_SHADER_RESOURCE_VIEW_DESC indexSrvDesc = {};
                                        indexSrvDesc.Format = DXGI_FORMAT_R32_UINT;
                                        indexSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
                                        indexSrvDesc.Shader4ComponentMapping =
                                            D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
                                        indexSrvDesc.Buffer.NumElements = m_TileCountX * m_TileCountY * MAX_LIGHTS_PER_TILE;
                                        indexSrvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
                                        d3dDevice->CreateShaderResourceView(
                                            static_cast<ID3D12Resource*>(m_LightIndexBuffer), &indexSrvDesc, fPlusDest8);
                                    }
                                    else
                                    {
                                        D3D12_SHADER_RESOURCE_VIEW_DESC nullBufDesc = {};
                                        nullBufDesc.Format = DXGI_FORMAT_R32_UINT;
                                        nullBufDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
                                        nullBufDesc.Shader4ComponentMapping =
                                            D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
                                        nullBufDesc.Buffer.NumElements = 1;
                                        nullBufDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;

                                        d3dDevice->CreateShaderResourceView(nullptr, &nullBufDesc, fPlusDest6);
                                        d3dDevice->CreateShaderResourceView(nullptr, &nullBufDesc, fPlusDest7);
                                        d3dDevice->CreateShaderResourceView(nullptr, &nullBufDesc, fPlusDest8);
                                    }

                                    tex->descriptorSignature = descriptorSignature;
                                    tex->descriptorUpdateFrame = m_FrameCounter;
                                }

                                WriteReflectionProbeDescriptors(submeshHeap, 10);

                                // 4. Bind Heaps (SRV + Sampler)
                                ID3D12DescriptorHeap* samplerHeap =
                                    m_SamplerHeap ? static_cast<ID3D12DescriptorHeap*>(m_SamplerHeap) : nullptr;
                                ID3D12DescriptorHeap* heaps[] = {submeshHeap, samplerHeap};
                                // We MUST bind 2 heaps if samplerHeap exists, otherwise simple rendering might fail
                                // sampling
                                cmdList->SetDescriptorHeaps(samplerHeap ? 2 : 1, heaps);

                                // 5. Bind Tables
                                // Root param 1: Material SRV table (t0-t3)
                                cmdList->SetGraphicsRootDescriptorTable(
                                    1, submeshHeap->GetGPUDescriptorHandleForHeapStart());
                                // Root param 2: Material sampler table (s0-s3)
                                if (samplerHeap)
                                    cmdList->SetGraphicsRootDescriptorTable(
                                        2, samplerHeap->GetGPUDescriptorHandleForHeapStart());

                                // Table 3: Shadow/SSAO (Starts at Slot 4)
                                D3D12_GPU_DESCRIPTOR_HANDLE shadowAOGpuHandle =
                                    submeshHeap->GetGPUDescriptorHandleForHeapStart();
                                shadowAOGpuHandle.ptr += 4 * srvSize;
                                cmdList->SetGraphicsRootDescriptorTable(3, shadowAOGpuHandle);

                                // Table 4: Forward+ (Starts at Slot 7)
                                D3D12_GPU_DESCRIPTOR_HANDLE fPlusGpuHandle =
                                    submeshHeap->GetGPUDescriptorHandleForHeapStart();
                                fPlusGpuHandle.ptr += 7 * srvSize;
                                cmdList->SetGraphicsRootDescriptorTable(4, fPlusGpuHandle);
                            }
                        }
                    }
                    else
                    {
                        // Fallback: bind shadow/AO table so lighting remains valid.
                        if (m_ShadowSRVHeap)
                        {
                            ID3D12DescriptorHeap* shadowHeap = static_cast<ID3D12DescriptorHeap*>(m_ShadowSRVHeap);
                            ID3D12DescriptorHeap* samplerHeap = static_cast<ID3D12DescriptorHeap*>(m_SamplerHeap);
                            ID3D12DescriptorHeap* heaps[] = {shadowHeap, samplerHeap};
                            cmdList->SetDescriptorHeaps(heaps[1] ? 2 : 1, heaps);
                            if (samplerHeap)
                                cmdList->SetGraphicsRootDescriptorTable(2, samplerHeap->GetGPUDescriptorHandleForHeapStart());

                            UINT localSrvSize =
                                d3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
                            WriteReflectionProbeDescriptors(shadowHeap, 10);
                            D3D12_GPU_DESCRIPTOR_HANDLE shadowAOGpuHandle = shadowHeap->GetGPUDescriptorHandleForHeapStart();
                            shadowAOGpuHandle.ptr += 4 * localSrvSize;
                            cmdList->SetGraphicsRootDescriptorTable(3, shadowAOGpuHandle);

                            D3D12_GPU_DESCRIPTOR_HANDLE forwardPlusGpuHandle = shadowHeap->GetGPUDescriptorHandleForHeapStart();
                            forwardPlusGpuHandle.ptr += 7 * localSrvSize;
                            cmdList->SetGraphicsRootDescriptorTable(4, forwardPlusGpuHandle);
                        }
                    }
                }
                else
                {
                    // Texture loading failed/pending - Bind Fallback to prevent Freeze
                    if (m_ShadowSRVHeap)
                    {
                        ID3D12DescriptorHeap* shadowHeap = static_cast<ID3D12DescriptorHeap*>(m_ShadowSRVHeap);
                        ID3D12DescriptorHeap* samplerHeap = static_cast<ID3D12DescriptorHeap*>(m_SamplerHeap);
                        ID3D12DescriptorHeap* heaps[] = {shadowHeap, samplerHeap};
                        cmdList->SetDescriptorHeaps(heaps[1] ? 2 : 1, heaps);
                        if (samplerHeap)
                            cmdList->SetGraphicsRootDescriptorTable(2, samplerHeap->GetGPUDescriptorHandleForHeapStart());

                        UINT localSrvSize =
                            d3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
                        WriteReflectionProbeDescriptors(shadowHeap, 10);
                        D3D12_GPU_DESCRIPTOR_HANDLE shadowAOGpuHandle = shadowHeap->GetGPUDescriptorHandleForHeapStart();
                        shadowAOGpuHandle.ptr += 4 * localSrvSize;
                        cmdList->SetGraphicsRootDescriptorTable(3, shadowAOGpuHandle);

                        D3D12_GPU_DESCRIPTOR_HANDLE forwardPlusGpuHandle = shadowHeap->GetGPUDescriptorHandleForHeapStart();
                        forwardPlusGpuHandle.ptr += 7 * localSrvSize;
                        cmdList->SetGraphicsRootDescriptorTable(4, forwardPlusGpuHandle);
                    }
                }
            }

            for (const PrimitiveMesh::RenderSection& section : batch.sections)
            {
                if (section.indexCount == 0)
                    continue;

                RecordLodDraw(renderLodLevel);
                cmdList->DrawIndexedInstanced(section.indexCount, 1, section.indexStart, 0, 0);
            }
        }
    }

    if (albedoTextureSlot >= 0 && albedoTextureSlot < 4)
    {
        if (texturePaths[albedoTextureSlot].empty() && !(mesh.lodHasTexturedSubmeshes[0] && albedoTextureSlot == 0))
            albedoTextureSlot = -1;
    }
    if (normalTextureSlot >= 0 && normalTextureSlot < 4 && texturePaths[normalTextureSlot].empty())
        normalTextureSlot = -1;
    if (ormTextureSlot >= 0 && ormTextureSlot < 4 && texturePaths[ormTextureSlot].empty())
        ormTextureSlot = -1;

    cbData.albedoTextureSlot = static_cast<float>(albedoTextureSlot);
    cbData.normalTextureSlot = static_cast<float>(normalTextureSlot);
    cbData.ormTextureSlot = static_cast<float>(ormTextureSlot);
    else
    {
        // Single material: draw entire mesh
        RecordLodDraw(renderLodLevel);
        cmdList->DrawIndexedInstanced(selectedIndexCount, 1, 0, 0, 0);
    }
}

void SimpleRenderer::Render(const Camera& camera, RHISwapChain* swapChain, float posX, float posY, float posZ,
                            float rotX, float rotY, float rotZ, float scaleX, float scaleY, float scaleZ)
{
    if (!m_Initialized || !swapChain)
        return;

    // Build world matrix from components
    const float deg2rad = 3.14159265f / 180.0f;
    float rx = rotX * deg2rad, ry = rotY * deg2rad, rz = rotZ * deg2rad;
    float cx = std::cos(rx), sx = std::sin(rx);
    float cy = std::cos(ry), sy = std::sin(ry);
    float cz = std::cos(rz), sz = std::sin(rz);

    float worldMatrix[16];
    worldMatrix[0] = cy * cz * scaleX;
    worldMatrix[1] = cy * sz * scaleX;
    worldMatrix[2] = -sy * scaleX;
    worldMatrix[3] = 0;
    worldMatrix[4] = (sx * sy * cz - cx * sz) * scaleY;
    worldMatrix[5] = (sx * sy * sz + cx * cz) * scaleY;
    worldMatrix[6] = sx * cy * scaleY;
    worldMatrix[7] = 0;
    worldMatrix[8] = (cx * sy * cz + sx * sz) * scaleZ;
    worldMatrix[9] = (cx * sy * sz - sx * cz) * scaleZ;
    worldMatrix[10] = cx * cy * scaleZ;
    worldMatrix[11] = 0;
    worldMatrix[12] = posX;
    worldMatrix[13] = posY;
    worldMatrix[14] = posZ;
    worldMatrix[15] = 1;

    Render(camera, swapChain, worldMatrix, PrimitiveType::Cube);
}

uint32_t SimpleRenderer::RenderMeshInstanced(const Camera& camera, RHISwapChain* swapChain, const PrimitiveMesh& mesh,
                                             const std::vector<const float*>& worldMatrices)
{
    if (worldMatrices.empty())
        return 0;

    // Conservative fallback path for unsupported permutations.
    auto& viewSettings = ViewSettings::Get();
    const bool hasMaterialTextures = !m_MaterialData.texturePaths[0].empty() || !m_MaterialData.texturePaths[1].empty() ||
                                     !m_MaterialData.texturePaths[2].empty() || !m_MaterialData.texturePaths[3].empty();
    const bool unsupported = !m_Initialized || !swapChain || !m_InstancedPipelineState || !m_InstanceCBResource ||
                             m_IsDepthPrepass || viewSettings.debugVisMode != DebugVisMode::Lit ||
                             m_MaterialData.materialShaderId != 0 || !mesh.submeshes.empty() ||
                             !mesh.colormapPath.empty() || hasMaterialTextures;

    if (unsupported)
    {
        for (const float* worldMatrix : worldMatrices)
        {
            RenderMesh(camera, swapChain, worldMatrix, mesh);
        }
        return static_cast<uint32_t>(worldMatrices.size());
    }

    ID3D12GraphicsCommandList* cmdList = GetD3D12CommandList(m_Device);
    ID3D12Device* d3dDevice = GetD3D12DevicePtr(m_Device);
    if (!cmdList || !d3dDevice || !m_MappedLightBuffer || !m_MappedInstanceBuffer || !m_CBResource)
    {
        for (const float* worldMatrix : worldMatrices)
        {
            RenderMesh(camera, swapChain, worldMatrix, mesh);
        }
        return static_cast<uint32_t>(worldMatrices.size());
    }

    D3D12SwapChain* d3dSwapChain = static_cast<D3D12SwapChain*>(swapChain);

    D3D12_VIEWPORT viewport = {};
    viewport.Width = static_cast<float>(swapChain->GetWidth());
    viewport.Height = static_cast<float>(swapChain->GetHeight());
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;
    cmdList->RSSetViewports(1, &viewport);

    D3D12_RECT scissor = {};
    scissor.right = static_cast<LONG>(swapChain->GetWidth());
    scissor.bottom = static_cast<LONG>(swapChain->GetHeight());
    cmdList->RSSetScissorRects(1, &scissor);

    D3D12_CPU_DESCRIPTOR_HANDLE rtv =
        m_IsInFXAAPass ? reinterpret_cast<ID3D12DescriptorHeap*>(m_FXAARTVHeap)->GetCPUDescriptorHandleForHeapStart()
                       : d3dSwapChain->GetCurrentRTV();
    D3D12_CPU_DESCRIPTOR_HANDLE dsv = d3dSwapChain->GetDSV();
    cmdList->OMSetRenderTargets(1, &rtv, FALSE, &dsv);

    cmdList->SetGraphicsRootSignature(reinterpret_cast<ID3D12RootSignature*>(m_RootSignature));
    cmdList->SetPipelineState(reinterpret_cast<ID3D12PipelineState*>(m_InstancedPipelineState));

    if (!mesh.vertexBuffer || !mesh.indexBuffer || !GetD3D12Buffer(mesh.vertexBuffer.get()) ||
        !GetD3D12Buffer(mesh.indexBuffer.get()))
    {
        return false;
    }

    uint32_t lodLevel = 0;
    const bool canUseAnyLod = HasGeneratedMeshLod(mesh, 1) || HasGeneratedMeshLod(mesh, 2);
    if (canUseAnyLod)
    {
        float lod1Threshold = 0.0f;
        float lod2Threshold = 0.0f;
        GetMeshLodScreenHeightThresholds(mesh, ViewSettings::Get().lodAggressiveness, lod1Threshold, lod2Threshold);
        float maxProjectedHeight = 0.0f;
        for (const float* wm : worldMatrices)
        {
            if (!wm)
                continue;
            maxProjectedHeight = std::max(maxProjectedHeight, EstimateProjectedScreenHeight(camera, mesh, wm));
        }
        if (maxProjectedHeight < lod2Threshold && mesh.lodIndexCounts[2] > 0)
            lodLevel = 2;
        else if (maxProjectedHeight < lod1Threshold && mesh.lodIndexCounts[1] > 0)
            lodLevel = 1;
    }

    RHIBufferPtr selectedVertexBuffer =
        (lodLevel < PrimitiveMesh::kLodCount && mesh.lodVertexBuffers[lodLevel] && HasGeneratedMeshLod(mesh, lodLevel))
            ? mesh.lodVertexBuffers[lodLevel]
            : mesh.vertexBuffer;
    RHIBufferPtr selectedIndexBuffer =
        (lodLevel < PrimitiveMesh::kLodCount && mesh.lodIndexBuffers[lodLevel] && HasGeneratedMeshLod(mesh, lodLevel))
            ? mesh.lodIndexBuffers[lodLevel]
            : mesh.indexBuffer;
    uint32_t selectedIndexCount =
        (lodLevel < PrimitiveMesh::kLodCount && mesh.lodIndexCounts[lodLevel] > 0 && HasGeneratedMeshLod(mesh, lodLevel))
            ? mesh.lodIndexCounts[lodLevel]
            : mesh.indexCount;

    if (!selectedVertexBuffer || !selectedIndexBuffer || !GetD3D12Buffer(selectedVertexBuffer.get()) ||
        !GetD3D12Buffer(selectedIndexBuffer.get()))
        return false;

    D3D12_VERTEX_BUFFER_VIEW vbView = {};
    vbView.BufferLocation = GetD3D12BufferGPUAddress(selectedVertexBuffer.get());
    vbView.SizeInBytes = static_cast<UINT>(selectedVertexBuffer->GetSize());
    vbView.StrideInBytes = sizeof(PrimitiveVertex);
    cmdList->IASetVertexBuffers(0, 1, &vbView);

    D3D12_INDEX_BUFFER_VIEW ibView = {};
    ibView.BufferLocation = GetD3D12BufferGPUAddress(selectedIndexBuffer.get());
    ibView.SizeInBytes = static_cast<UINT>(selectedIndexBuffer->GetSize());
    ibView.Format = DXGI_FORMAT_R32_UINT;
    cmdList->IASetIndexBuffer(&ibView);
    cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    // Prepare shared lighting/material constants once for the batch.
    ConstantBufferData cbData = {};
    const float* vp = camera.GetViewProjectionMatrix();
    std::memcpy(cbData.mvpMatrix, vp, sizeof(cbData.mvpMatrix));
    static const float kIdentity[16] = {
        1, 0, 0, 0,
        0, 1, 0, 0,
        0, 0, 1, 0,
        0, 0, 0, 1,
    };
    std::memcpy(cbData.worldMatrix, kIdentity, sizeof(cbData.worldMatrix));

    // Directional Light
    cbData.dirLightDir[0] = m_LightData.lightDirX;
    cbData.dirLightDir[1] = m_LightData.lightDirY;
    cbData.dirLightDir[2] = m_LightData.lightDirZ;
    cbData.dirLightIntensity = m_LightData.lightIntensity;
    cbData.dirLightColor[0] = m_LightData.lightColorR;
    cbData.dirLightColor[1] = m_LightData.lightColorG;
    cbData.dirLightColor[2] = m_LightData.lightColorB;
    cbData.ambientColor[0] = m_LightData.ambientColorR;
    cbData.ambientColor[1] = m_LightData.ambientColorG;
    cbData.ambientColor[2] = m_LightData.ambientColorB;
    cbData.ambientIntensity = m_LightData.ambientIntensity;
    cbData.numPointLights = m_LightData.numPointLights;
    cbData.numSpotLights = m_LightData.numSpotLights;

    for (int i = 0; i < 16; ++i)
    {
        cbData.pointLights[i].posX = m_LightData.pointLights[i].posX;
        cbData.pointLights[i].posY = m_LightData.pointLights[i].posY;
        cbData.pointLights[i].posZ = m_LightData.pointLights[i].posZ;
        cbData.pointLights[i].range = m_LightData.pointLights[i].range;
        cbData.pointLights[i].colorR = m_LightData.pointLights[i].colorR;
        cbData.pointLights[i].colorG = m_LightData.pointLights[i].colorG;
        cbData.pointLights[i].colorB = m_LightData.pointLights[i].colorB;
        cbData.pointLights[i].intensity = m_LightData.pointLights[i].intensity;
        cbData.pointLights[i].shadowEnabled = m_LightData.pointLights[i].shadowEnabled;
        cbData.pointLights[i].shadowBaseSlice = m_LightData.pointLights[i].shadowBaseSlice;
        cbData.pointLights[i].shadowBias = m_LightData.pointLights[i].shadowBias;
        cbData.pointLights[i]._pad = 0.0f;

        cbData.spotLights[i].posX = m_LightData.spotLights[i].posX;
        cbData.spotLights[i].posY = m_LightData.spotLights[i].posY;
        cbData.spotLights[i].posZ = m_LightData.spotLights[i].posZ;
        cbData.spotLights[i].range = m_LightData.spotLights[i].range;
        cbData.spotLights[i].dirX = m_LightData.spotLights[i].dirX;
        cbData.spotLights[i].dirY = m_LightData.spotLights[i].dirY;
        cbData.spotLights[i].dirZ = m_LightData.spotLights[i].dirZ;
        cbData.spotLights[i].innerCos = m_LightData.spotLights[i].innerCos;
        cbData.spotLights[i].colorR = m_LightData.spotLights[i].colorR;
        cbData.spotLights[i].colorG = m_LightData.spotLights[i].colorG;
        cbData.spotLights[i].colorB = m_LightData.spotLights[i].colorB;
        cbData.spotLights[i].outerCos = m_LightData.spotLights[i].outerCos;
        cbData.spotLights[i].intensity = m_LightData.spotLights[i].intensity;
        cbData.spotLights[i].shadowEnabled = m_LightData.spotLights[i].shadowEnabled;
        cbData.spotLights[i].shadowBaseSlice = m_LightData.spotLights[i].shadowBaseSlice;
        cbData.spotLights[i].shadowBias = m_LightData.spotLights[i].shadowBias;
    }

    cbData.materialColor[0] = m_MaterialData.colorR;
    cbData.materialColor[1] = m_MaterialData.colorG;
    cbData.materialColor[2] = m_MaterialData.colorB;
    const bool showLodVisualization =
        viewSettings.debugVisMode == DebugVisMode::LODVisualization || viewSettings.lodDebugTint;
    SetLodDebugTint(cbData.lodDebugTint, showLodVisualization, lodLevel);
    cbData.materialMetallic = m_MaterialData.metallic;
    cbData.materialRoughness = m_MaterialData.roughness;
    cbData.materialAmbientOcclusion = m_MaterialData.ambientOcclusion;
    cbData.materialEmissiveColor[0] = m_MaterialData.emissiveColorR;
    cbData.materialEmissiveColor[1] = m_MaterialData.emissiveColorG;
    cbData.materialEmissiveColor[2] = m_MaterialData.emissiveColorB;
    cbData.materialEmissiveStrength = m_MaterialData.emissiveStrength;
    cbData.hasTextures[0] = 0.0f;
    cbData.hasTextures[1] = 0.0f;
    cbData.hasTextures[2] = 0.0f;
    cbData.hasTextures[3] = 0.0f;
    cbData.albedoTextureSlot = mesh.lodHasTexturedSubmeshes[0] ? 0.0f : static_cast<float>(m_MaterialData.albedoTextureSlot);
    cbData.normalTextureSlot = static_cast<float>(m_MaterialData.normalTextureSlot);
    cbData.ormTextureSlot = static_cast<float>(m_MaterialData.ormTextureSlot);
    cbData.uvTiling[0] = m_MaterialData.tilingU;
    cbData.uvTiling[1] = m_MaterialData.tilingV;
    cbData.uvOffset[0] = m_MaterialData.offsetU;
    cbData.uvOffset[1] = m_MaterialData.offsetV;
    cbData.pannerSpeed[0] = m_MaterialData.pannerSpeedU;
    cbData.pannerSpeed[1] = m_MaterialData.pannerSpeedV;
    cbData.pannerMethod = m_MaterialData.pannerMethod;
    cbData.pannerLink = m_MaterialData.pannerLink ? 1 : 0;
    cbData.lightmapUvScale[0] = m_MaterialData.lightmapScaleU;
    cbData.lightmapUvScale[1] = m_MaterialData.lightmapScaleV;
    cbData.lightmapUvOffset[0] = m_MaterialData.lightmapOffsetU;
    cbData.lightmapUvOffset[1] = m_MaterialData.lightmapOffsetV;
    cbData.lightmapEnabled = 0.0f;
    cbData.lightmapTextureSlot = -1.0f;
    cbData.lightmapIntensity = m_MaterialData.lightmapIntensity;

    float camX, camY, camZ;
    const_cast<Camera&>(camera).GetPosition(camX, camY, camZ);
    cbData.cameraPos[0] = camX;
    cbData.cameraPos[1] = camY;
    cbData.cameraPos[2] = camZ;
    cbData.time = m_ElapsedTime;

    std::memcpy(cbData.shadowMatrix, m_ShadowLightMatrix, sizeof(m_ShadowLightMatrix));
    cbData.shadowBias = m_ShadowBias;
    cbData.shadowEnabled = m_ShadowEnabled ? 1.0f : 0.0f;
    cbData.viewportWidth = static_cast<float>(swapChain->GetWidth());
    cbData.viewportHeight = static_cast<float>(swapChain->GetHeight());
    const bool forwardPlusActive =
        m_ForwardPlusEnabled && m_ForwardPlusSRVHeap && m_LightGridBuffer && m_LightIndexBuffer;
    cbData.fPlusEnabled = forwardPlusActive ? 1.0f : 0.0f;
    cbData.tileCountX = m_TileCountX;
    cbData.tileCountY = m_TileCountY;
    cbData.debugVisMode = static_cast<uint32_t>(ViewSettings::Get().debugVisMode);
    cbData.ssaoEnabled = (m_SSAOSettings.enabled && !m_SceneCaptureMode && m_SSAOSRVHeap && m_SSAOBlurredRT) ? 1.0f : 0.0f;

    const uint32_t alignedSize = (sizeof(ConstantBufferData) + 255) & ~255u;
    if (m_CurrentCBOffset + alignedSize > kMaxCBSize)
        return 0;

    uint8_t* targetPtr = static_cast<uint8_t*>(m_MappedLightBuffer) + m_CurrentCBOffset;
    std::memcpy(targetPtr, &cbData, sizeof(ConstantBufferData));
    auto* cbRes = static_cast<ID3D12Resource*>(m_CBResource);
    cmdList->SetGraphicsRootConstantBufferView(0, cbRes->GetGPUVirtualAddress() + m_CurrentCBOffset);
    m_CurrentCBOffset += alignedSize;

    // Bind shadow + SSAO descriptor table / samplers (same as non-textured path).
    if (m_ShadowSRVHeap)
    {
        ID3D12DescriptorHeap* shadowSrvHeap = static_cast<ID3D12DescriptorHeap*>(m_ShadowSRVHeap);
        ID3D12DescriptorHeap* samplerHeap = static_cast<ID3D12DescriptorHeap*>(m_SamplerHeap);
        ID3D12DescriptorHeap* heaps[] = {shadowSrvHeap, samplerHeap};
        cmdList->SetDescriptorHeaps(samplerHeap ? 2 : 1, heaps);
        if (samplerHeap)
            cmdList->SetGraphicsRootDescriptorTable(2, samplerHeap->GetGPUDescriptorHandleForHeapStart());

        UINT srvSize = d3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        if (m_SSAOSRVHeap && m_SharedDescriptorFrame != m_FrameCounter)
        {
            ID3D12DescriptorHeap* ssaoHeap = static_cast<ID3D12DescriptorHeap*>(m_SSAOSRVHeap);
            D3D12_CPU_DESCRIPTOR_HANDLE ssaoSrc = ssaoHeap->GetCPUDescriptorHandleForHeapStart();
            ssaoSrc.ptr += 3 * srvSize;
            D3D12_CPU_DESCRIPTOR_HANDLE ssaoDest = shadowSrvHeap->GetCPUDescriptorHandleForHeapStart();
            ssaoDest.ptr += 5 * srvSize;
            d3dDevice->CopyDescriptorsSimple(1, ssaoDest, ssaoSrc, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
            m_SharedDescriptorFrame = m_FrameCounter;
        }

        D3D12_GPU_DESCRIPTOR_HANDLE shadowAOGpuHandle = shadowSrvHeap->GetGPUDescriptorHandleForHeapStart();
        shadowAOGpuHandle.ptr += 4 * srvSize;
        cmdList->SetGraphicsRootDescriptorTable(3, shadowAOGpuHandle);

        if (forwardPlusActive)
        {
            ID3D12DescriptorHeap* fPlusHeap = static_cast<ID3D12DescriptorHeap*>(m_ForwardPlusSRVHeap);

            D3D12_CPU_DESCRIPTOR_HANDLE fPlusDest = shadowSrvHeap->GetCPUDescriptorHandleForHeapStart();
            fPlusDest.ptr += 7 * srvSize;

            D3D12_CPU_DESCRIPTOR_HANDLE lightSrc = fPlusHeap->GetCPUDescriptorHandleForHeapStart();
            lightSrc.ptr += 1 * srvSize;
            d3dDevice->CopyDescriptorsSimple(1, fPlusDest, lightSrc, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

            fPlusDest.ptr += 1 * srvSize;
            D3D12_SHADER_RESOURCE_VIEW_DESC gridSrvDesc = {};
            gridSrvDesc.Format = DXGI_FORMAT_R32_UINT;
            gridSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
            gridSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            gridSrvDesc.Buffer.NumElements = m_TileCountX * m_TileCountY * 2;
            gridSrvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
            d3dDevice->CreateShaderResourceView(reinterpret_cast<ID3D12Resource*>(m_LightGridBuffer), &gridSrvDesc,
                                                fPlusDest);

            fPlusDest.ptr += 1 * srvSize;
            D3D12_SHADER_RESOURCE_VIEW_DESC indexSrvDesc = {};
            indexSrvDesc.Format = DXGI_FORMAT_R32_UINT;
            indexSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
            indexSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            indexSrvDesc.Buffer.NumElements = m_TileCountX * m_TileCountY * MAX_LIGHTS_PER_TILE;
            indexSrvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
            d3dDevice->CreateShaderResourceView(reinterpret_cast<ID3D12Resource*>(m_LightIndexBuffer),
                                                &indexSrvDesc, fPlusDest);

            D3D12_GPU_DESCRIPTOR_HANDLE forwardPlusGpuHandle = shadowSrvHeap->GetGPUDescriptorHandleForHeapStart();
            forwardPlusGpuHandle.ptr += 7 * srvSize;
            cmdList->SetGraphicsRootDescriptorTable(4, forwardPlusGpuHandle);
        }
    }

    auto* instanceCbRes = static_cast<ID3D12Resource*>(m_InstanceCBResource);
    uint32_t drawCalls = 0;
    for (size_t start = 0; start < worldMatrices.size(); start += kMaxInstanceBatch)
    {
        const size_t count = std::min<size_t>(kMaxInstanceBatch, worldMatrices.size() - start);
        float* dst = static_cast<float*>(m_MappedInstanceBuffer);
        for (size_t i = 0; i < count; ++i)
        {
            const float* src = worldMatrices[start + i];
            std::memcpy(dst + (i * 16), src, sizeof(float) * 16);
        }

        cmdList->SetGraphicsRootConstantBufferView(5, instanceCbRes->GetGPUVirtualAddress());
        RecordLodDraw(lodLevel);
        cmdList->DrawIndexedInstanced(selectedIndexCount, static_cast<UINT>(count), 0, 0, 0);
        ++drawCalls;
    }

    return drawCalls;
}

void SimpleRenderer::Render(const Camera& camera, RHISwapChain* swapChain, const float* worldMatrix)
{
    Render(camera, swapChain, worldMatrix, PrimitiveType::Cube);
}

void SimpleRenderer::Render(const Camera& camera, RHISwapChain* swapChain, const float* worldMatrix,
                            PrimitiveType primitiveType)
{
    size_t index = static_cast<size_t>(primitiveType);
    if (index >= kPrimitiveCount)
        index = 0; // Default to cube
    RenderMesh(camera, swapChain, worldMatrix, m_Meshes[static_cast<size_t>(primitiveType)]);
}

void SimpleRenderer::RenderMesh(const Camera& camera, RHISwapChain* swapChain, const float* worldMatrix,
                                const PrimitiveMesh& mesh, const MaterialData& material)
{
    // Set material and render
    m_MaterialData = material;
    RenderMesh(camera, swapChain, worldMatrix, mesh);
}

Camera::Frustum SimpleRenderer::ComputeShadowFrustum(const Camera& camera)
{
    Camera::Frustum frustum;

    // Use light direction from m_LightData
    float ldirX = m_LightData.lightDirX;
    float ldirY = m_LightData.lightDirY;
    float ldirZ = m_LightData.lightDirZ;

    // Normalize light direction
    float len = std::sqrt(ldirX * ldirX + ldirY * ldirY + ldirZ * ldirZ);
    if (len < 0.0001f)
    {
        ldirX = 0.5f;
        ldirY = -0.7f;
        ldirZ = 0.5f;
        len = 1.0f;
    }
    ldirX /= len;
    ldirY /= len;
    ldirZ /= len;

    // Light position: camera target + offset along light direction
    float targetX, targetY, targetZ;
    camera.GetTarget(targetX, targetY, targetZ);

    float lightPosX = targetX - ldirX * m_ShadowDistance;
    float lightPosY = targetY - ldirY * m_ShadowDistance;
    float lightPosZ = targetZ - ldirZ * m_ShadowDistance;

    // Build light view matrix
    float upX = 0.0f, upY = 1.0f, upZ = 0.0f;
    if (std::abs(ldirY) > 0.99f)
    {
        upX = 0.0f;
        upY = 0.0f;
        upZ = 1.0f;
    }

    float fwdX = ldirX, fwdY = ldirY, fwdZ = ldirZ;
    float rightX = upY * fwdZ - upZ * fwdY;
    float rightY = upZ * fwdX - upX * fwdZ;
    float rightZ = upX * fwdY - upY * fwdX;
    float rightLen = std::sqrt(rightX * rightX + rightY * rightY + rightZ * rightZ);
    rightX /= rightLen;
    rightY /= rightLen;
    rightZ /= rightLen;

    float upX2 = fwdY * rightZ - fwdZ * rightY;
    float upY2 = fwdZ * rightX - fwdX * rightZ;
    float upZ2 = fwdX * rightY - fwdY * rightX;

    float lightView[16] = {rightX,
                           upX2,
                           fwdX,
                           0.0f,
                           rightY,
                           upY2,
                           fwdY,
                           0.0f,
                           rightZ,
                           upZ2,
                           fwdZ,
                           0.0f,
                           -(rightX * lightPosX + rightY * lightPosY + rightZ * lightPosZ),
                           -(upX2 * lightPosX + upY2 * lightPosY + upZ2 * lightPosZ),
                           -(fwdX * lightPosX + fwdY * lightPosY + fwdZ * lightPosZ),
                           1.0f};

    // Orthographic projection
    float orthoSize = std::max(10.0f, m_ShadowDistance * 0.1f);
    float nearPlane = 1.0f;
    float farPlane = m_ShadowDistance * 3.0f;
    float lightProj[16] = {1.0f / orthoSize,
                           0.0f,
                           0.0f,
                           0.0f,
                           0.0f,
                           1.0f / orthoSize,
                           0.0f,
                           0.0f,
                           0.0f,
                           0.0f,
                           1.0f / (farPlane - nearPlane),
                           0.0f,
                           0.0f,
                           0.0f,
                           -nearPlane / (farPlane - nearPlane),
                           1.0f};

    // Multiply: lightVP = lightProj * lightView
    float lightVP[16];
    for (int col = 0; col < 4; ++col)
    {
        for (int row = 0; row < 4; ++row)
        {
            lightVP[col * 4 + row] = 0.0f;
            for (int k = 0; k < 4; ++k)
            {
                lightVP[col * 4 + row] += lightProj[k * 4 + row] * lightView[col * 4 + k];
            }
        }
    }

    // Extract frustum planes from the light's view-projection matrix (Gribb-Hartmann)
    const float* m = lightVP;

    // Left: row3 + row0
    frustum.planes[0].a = m[3] + m[0];
    frustum.planes[0].b = m[7] + m[4];
    frustum.planes[0].c = m[11] + m[8];
    frustum.planes[0].d = m[15] + m[12];
    frustum.planes[0].Normalize();

    // Right: row3 - row0
    frustum.planes[1].a = m[3] - m[0];
    frustum.planes[1].b = m[7] - m[4];
    frustum.planes[1].c = m[11] - m[8];
    frustum.planes[1].d = m[15] - m[12];
    frustum.planes[1].Normalize();

    // Bottom: row3 + row1
    frustum.planes[2].a = m[3] + m[1];
    frustum.planes[2].b = m[7] + m[5];
    frustum.planes[2].c = m[11] + m[9];
    frustum.planes[2].d = m[15] + m[13];
    frustum.planes[2].Normalize();

    // Top: row3 - row1
    frustum.planes[3].a = m[3] - m[1];
    frustum.planes[3].b = m[7] - m[5];
    frustum.planes[3].c = m[11] - m[9];
    frustum.planes[3].d = m[15] - m[13];
    frustum.planes[3].Normalize();

    // Near: row2
    frustum.planes[4].a = m[2];
    frustum.planes[4].b = m[6];
    frustum.planes[4].c = m[10];
    frustum.planes[4].d = m[14];
    frustum.planes[4].Normalize();

    // Far: row3 - row2
    frustum.planes[5].a = m[3] - m[2];
    frustum.planes[5].b = m[7] - m[6];
    frustum.planes[5].c = m[11] - m[10];
    frustum.planes[5].d = m[15] - m[14];
    frustum.planes[5].Normalize();

    return frustum;
}

void SimpleRenderer::RenderShadowMap(const Camera& camera, RHISwapChain* /*swapChain*/,
                                     const std::vector<std::pair<const float*, const PrimitiveMesh*>>& shadowCasters)
{
    DirectionalShadowGraphPassExecutor::Execute(*this, camera, shadowCasters);
}

void SimpleRenderer::RenderLocalLightShadowMaps(
    RHISwapChain* /*swapChain*/, const std::vector<std::pair<const float*, const PrimitiveMesh*>>& shadowCasters)
{
    LocalShadowGraphPassExecutor::Execute(*this, shadowCasters);
}

// Returns a vector of pointers for each split mesh entity
// Returns a vector of pointers for each split mesh entity
std::vector<PrimitiveMesh*> SimpleRenderer::LoadMesh(const std::string& filepath)
{
    std::vector<PrimitiveMesh*> result;

    // Check cache first
    auto it = m_MeshCache.find(filepath);
    if (it != m_MeshCache.end())
    {
        for (auto& meshPtr : it->second)
        {
            if (meshPtr)
                result.push_back(meshPtr.get());
        }
        return result;
    }

    // Build full path using unified asset manager logic
    std::string fullPathStr = AssetManager::Get().GetFullPath(filepath);
    std::filesystem::path fullPath(fullPathStr);

    // Load based on extension
    std::vector<MeshData> meshesData;
    std::string colormapPath;
    bool loadSuccess = false;

    std::string ext = fullPath.extension().string();
    for (auto& c : ext)
        c = (char)tolower(c);

    if (ext == ".fbx")
    {
        // Extract base name from filepath for material naming
        std::string meshBaseName = fullPath.stem().string();
        loadSuccess = LoadFbxFile(fullPath.string(), meshesData, colormapPath, meshBaseName);
    }
    else
    {
        // Default to OBJ (single mesh)
        MeshData singleMesh;
        loadSuccess = LoadObjFile(fullPath.string(), singleMesh);
        if (loadSuccess)
            meshesData.push_back(singleMesh);
    }

    if (!loadSuccess || meshesData.empty())
    {
        std::printf("Failed to load mesh: %s\n", filepath.c_str());
        return result; // Empty
    }

    // Create entry in cache
    MeshGroup& cacheGroup = m_MeshCache[filepath];

    for (size_t i = 0; i < meshesData.size(); ++i)
    {
        auto& data = meshesData[i];
        OptimizeMeshSubmeshesByMaterial(data);

        // Create GPU buffers
        auto newMesh = std::make_unique<PrimitiveMesh>();
        if (CreateMeshBuffers(data, *newMesh))
        {
            // Copy submeshes
            newMesh->submeshes = data.submeshes;

            // Pre-resolve textures if needed
            for (auto& sub : newMesh->submeshes)
            {
                if (sub.texturePath.empty() && !sub.materialPath.empty())
                {
                    // sub.texturePath = GetSubmeshTexturePath(sub.materialPath);
                }
            }

            // Only set map path on first mesh or if we have one?
            // Legacy colormapPath is per-file, maybe set only on first one or all?
            newMesh->colormapPath = colormapPath;

            PrimitiveMesh* ptr = newMesh.get();
            cacheGroup.push_back(std::move(newMesh));
            result.push_back(ptr);
        }
        else
        {
            std::printf("Failed to create GPU buffers for mesh part %zu: %s\n", i, filepath.c_str());
        }
    }

    if (result.empty())
    {
        m_MeshCache.erase(filepath);
    }
    else
    {
        std::printf("Loaded mesh '%s' as %zu entities\n", filepath.c_str(), result.size());
    }

    return result;
}

void SimpleRenderer::RenderSkybox(const Camera& camera, RHISwapChain* swapChain, const std::string& cubemapPath,
                                  int wrapMode, float rotationDegrees, bool showMarkers, float topR, float topG,
                                  float topB, float bottomR, float bottomG, float bottomB)
{
    // Clamp wrap mode to valid range
    size_t wrapModeIdx =
        (wrapMode >= 0 && wrapMode < static_cast<int>(kSkyboxWrapModeCount)) ? static_cast<size_t>(wrapMode) : 0;

    if (!m_Initialized || !swapChain || !m_SkyboxPSO[wrapModeIdx] || !m_SkyboxRootSignature[wrapModeIdx] ||
        !m_SkyboxSRVHeap)
        return;

    // Ensure cube mesh is available
    if (!m_Meshes[0].vertexBuffer || !m_Meshes[0].indexBuffer)
        return;

    // Static cached path and marker state for reloading detection
    static std::string s_LoadedCubemapPath;
    static bool s_LoadedShowMarkers = false;
    static uint64_t s_LoadedCubemapStamp = 0;
    // Assuming m_CurrentSkyboxWrapMode is a member variable of SimpleRenderer, initialized to a default value (e.g.,
    // -1) in the constructor or declaration.

    // Check if we need to reload the cubemap (path changed or marker state changed)
    const uint64_t currentSourceStamp = showMarkers || cubemapPath.empty() ? 0 : ComputeCubemapSourceStamp(cubemapPath);
    bool needsReload = !m_SkyboxCubemap || cubemapPath != s_LoadedCubemapPath || showMarkers != s_LoadedShowMarkers ||
                       currentSourceStamp != s_LoadedCubemapStamp;

    CubemapData cubemapData;
    int faceSize = 0; // Declare faceSize outside the if block

    // Create or reload sky cubemap
    if (needsReload)
    {
        m_SkyboxCubemap = nullptr;
        s_LoadedCubemapPath.clear();
        s_LoadedCubemapStamp = 0;

        // Note: wrap mode changes require recreating both root signature AND PSO together
        // For now, wrap mode is stored but not applied at runtime (always uses CLAMP from CreatePipelineState)
        m_CurrentSkyboxWrapMode = wrapMode;

        // Need valid device to create texture
        if (!m_Device)
            return;

        // If markers are enabled, use debug marker cubemap
        if (showMarkers)
        {
            cubemapData = CreateMarkerCubemap(256);
            s_LoadedCubemapPath.clear();
            s_LoadedShowMarkers = true;
            s_LoadedCubemapStamp = 0;
        }
        // Otherwise try to load from file if path is provided
        else if (!cubemapPath.empty())
        {
            cubemapData = LoadCubemap(cubemapPath);
            if (cubemapData.IsValid())
            {
                s_LoadedCubemapPath = cubemapPath;
                s_LoadedCubemapStamp = currentSourceStamp;
                faceSize = cubemapData.width;
            }
            s_LoadedShowMarkers = false;
        }
        else
        {
            s_LoadedShowMarkers = false;
            s_LoadedCubemapStamp = 0;
        }

        // Fall back to procedural gradient if no path or loading failed
        if (!cubemapData.IsValid())
        {
            faceSize = 64;
            cubemapData.width = faceSize;
            cubemapData.height = faceSize;
            cubemapData.channels = 4;
            cubemapData.format = RHIFormat::R8G8B8A8_UNORM;
            cubemapData.bytesPerChannel = 1;

            // Generate gradient for each face
            for (int face = 0; face < 6; ++face)
            {
                cubemapData.faceData[face].resize(faceSize * faceSize * 4);

                for (int y = 0; y < faceSize; ++y)
                {
                    for (int x = 0; x < faceSize; ++x)
                    {
                        int idx = (y * faceSize + x) * 4;
                        float t = static_cast<float>(y) / (faceSize - 1);

                        uint8_t r, g, b;
                        if (face == 2) // +Y (top)
                        {
                            r = static_cast<uint8_t>(topR * 255);
                            g = static_cast<uint8_t>(topG * 255);
                            b = static_cast<uint8_t>(topB * 255);
                        }
                        else if (face == 3) // -Y (bottom)
                        {
                            r = static_cast<uint8_t>(bottomR * 255);
                            g = static_cast<uint8_t>(bottomG * 255);
                            b = static_cast<uint8_t>(bottomB * 255);
                        }
                        else // Side faces: interpolate
                        {
                            r = static_cast<uint8_t>((bottomR + (topR - bottomR) * (1.0f - t)) * 255);
                            g = static_cast<uint8_t>((bottomG + (topG - bottomG) * (1.0f - t)) * 255);
                            b = static_cast<uint8_t>((bottomB + (topB - bottomB) * (1.0f - t)) * 255);
                        }

                        cubemapData.faceData[face][idx + 0] = r;
                        cubemapData.faceData[face][idx + 1] = g;
                        cubemapData.faceData[face][idx + 2] = b;
                        cubemapData.faceData[face][idx + 3] = 255;
                    }
                }
            }
        }

        if (showMarkers)
        {
            faceSize = 256;
        }

        if (faceSize <= 0)
            faceSize = cubemapData.width;

        const uint32_t mipLevels =
            cubemapData.IsHdr()
                ? 1u
                : ComputeTextureMipCount(static_cast<uint32_t>(cubemapData.width),
                                         static_cast<uint32_t>(cubemapData.height));

        // Create texture with correct size
        RHITextureDesc texDesc;
        texDesc.width = static_cast<uint32_t>(cubemapData.width);
        texDesc.height = static_cast<uint32_t>(cubemapData.height);
        texDesc.depth = 1;
        texDesc.arrayLayers = 6; // 6 faces for cubemap
        texDesc.mipLevels = mipLevels;
        texDesc.format = cubemapData.format;
        texDesc.type = RHITextureType::TextureCube;
        texDesc.usage = RHITextureUsage::Sampled;
        texDesc.debugName = "SkyboxCubemap";

        m_SkyboxCubemap = m_Device->CreateTexture(texDesc);
        if (!m_SkyboxCubemap)
            return;

        // Keep upload buffers alive until the next reload or app close
        static std::vector<ComPtr<ID3D12Resource>> s_UploadBuffers;
        s_UploadBuffers.clear();

        ID3D12Device* d3dDevice = GetD3D12DevicePtr(m_Device);
        ID3D12GraphicsCommandList* cmdList = GetD3D12CommandList(m_Device);
        D3D12Texture* d3dTexture = static_cast<D3D12Texture*>(m_SkyboxCubemap.get());
        ID3D12Resource* textureRes = d3dTexture->GetResource();

        if (!UploadCubemapTexture(d3dDevice, cmdList, textureRes, cubemapData, mipLevels, s_UploadBuffers))
            return;

        // Create SRV for the cubemap
        auto* srvHeap = reinterpret_cast<ID3D12DescriptorHeap*>(m_SkyboxSRVHeap);

        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format = GetTextureSrvFormat(cubemapData.format);
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.TextureCube.MostDetailedMip = 0;
        srvDesc.TextureCube.MipLevels = mipLevels;
        srvDesc.TextureCube.ResourceMinLODClamp = 0.0f;

        d3dDevice->CreateShaderResourceView(textureRes, &srvDesc, srvHeap->GetCPUDescriptorHandleForHeapStart());

        // Save the wrap mode after successful creation to prevent infinite reload loop
        m_CurrentSkyboxWrapMode = wrapMode;
    }

    // Ensure we have a valid cubemap before rendering
    if (!m_SkyboxCubemap)
        return;

    // Ensure root signature is valid after potential recreation
    if (!m_SkyboxRootSignature[wrapModeIdx])
        return;

    ID3D12GraphicsCommandList* cmdList = GetD3D12CommandList(m_Device);
    auto* srvHeap = reinterpret_cast<ID3D12DescriptorHeap*>(m_SkyboxSRVHeap);
    D3D12SwapChain* d3dSwapChain = static_cast<D3D12SwapChain*>(swapChain);

    // Set viewport (CRITICAL - was missing!)
    D3D12_VIEWPORT viewport = {};
    viewport.Width = static_cast<float>(swapChain->GetWidth());
    viewport.Height = static_cast<float>(swapChain->GetHeight());
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;
    cmdList->RSSetViewports(1, &viewport);

    // Set scissor rect (CRITICAL - was missing!)
    D3D12_RECT scissor = {};
    scissor.right = static_cast<LONG>(swapChain->GetWidth());
    scissor.bottom = static_cast<LONG>(swapChain->GetHeight());
    cmdList->RSSetScissorRects(1, &scissor);

    // Set render targets (CRITICAL - was missing!)
    D3D12_CPU_DESCRIPTOR_HANDLE rtv =
        m_IsInFXAAPass ? reinterpret_cast<ID3D12DescriptorHeap*>(m_FXAARTVHeap)->GetCPUDescriptorHandleForHeapStart()
                       : d3dSwapChain->GetCurrentRTV();
    D3D12_CPU_DESCRIPTOR_HANDLE dsv = d3dSwapChain->GetDSV();
    cmdList->OMSetRenderTargets(1, &rtv, FALSE, &dsv);

    // Set skybox PSO and root signature for the selected wrap mode
    auto* skyboxPso = reinterpret_cast<ID3D12PipelineState*>(m_SkyboxPSO[wrapModeIdx]);
    auto* skyboxRootSig = reinterpret_cast<ID3D12RootSignature*>(m_SkyboxRootSignature[wrapModeIdx]);

    cmdList->SetPipelineState(skyboxPso);
    cmdList->SetGraphicsRootSignature(skyboxRootSig);

    // Set SRV descriptor heap and table
    ID3D12DescriptorHeap* heaps[] = {srvHeap};
    cmdList->SetDescriptorHeaps(1, heaps);
    cmdList->SetGraphicsRootDescriptorTable(1, srvHeap->GetGPUDescriptorHandleForHeapStart());

    // Prepare constant buffer data for skybox
    struct SkyboxCB
    {
        float ViewProjection[16];
        float CameraPos[3];
        float _pad0;
        float TintColor[3];
        float _pad1;
        float RotationParams[4];
    };

    SkyboxCB cb = {};
    const float rotRadians = rotationDegrees * 3.14159265f / 180.0f;
    memcpy(cb.ViewProjection, camera.GetViewProjectionMatrix(), 16 * sizeof(float));

    float camX, camY, camZ;
    const_cast<Camera&>(camera).GetPosition(camX, camY, camZ);
    cb.CameraPos[0] = camX;
    cb.CameraPos[1] = camY;
    cb.CameraPos[2] = camZ;
    cb.TintColor[0] = topR;
    cb.TintColor[1] = topG;
    cb.TintColor[2] = topB;
    cb.RotationParams[0] = std::cos(rotRadians);
    cb.RotationParams[1] = std::sin(rotRadians);
    cb.RotationParams[2] = 0.0f;
    cb.RotationParams[3] = 0.0f;

    // Upload constant buffer (use ring buffer offset)
    uint32_t cbOffset = m_CurrentCBOffset;
    m_CurrentCBOffset = (m_CurrentCBOffset + 256) % kMaxCBSize;

    if (!m_MappedLightBuffer || !m_CBResource)
        return;

    memcpy(static_cast<uint8_t*>(m_MappedLightBuffer) + cbOffset, &cb, sizeof(cb));

    D3D12_GPU_VIRTUAL_ADDRESS cbAddress =
        reinterpret_cast<ID3D12Resource*>(m_CBResource)->GetGPUVirtualAddress() + cbOffset;
    cmdList->SetGraphicsRootConstantBufferView(0, cbAddress);

    // Render the cube mesh
    auto& cubeMesh = m_Meshes[0];
    if (!cubeMesh.vertexBuffer || !cubeMesh.indexBuffer || !GetD3D12Buffer(cubeMesh.vertexBuffer.get()) ||
        !GetD3D12Buffer(cubeMesh.indexBuffer.get()))
        return;

    D3D12_VERTEX_BUFFER_VIEW vbView = {};
    vbView.BufferLocation = GetD3D12BufferGPUAddress(cubeMesh.vertexBuffer.get());
    vbView.SizeInBytes = static_cast<UINT>(cubeMesh.vertexBuffer->GetSize());
    vbView.StrideInBytes = sizeof(PrimitiveVertex);

    D3D12_INDEX_BUFFER_VIEW ibView = {};
    ibView.BufferLocation = GetD3D12BufferGPUAddress(cubeMesh.indexBuffer.get());
    ibView.SizeInBytes = static_cast<UINT>(cubeMesh.indexBuffer->GetSize());
    ibView.Format = DXGI_FORMAT_R32_UINT;

    cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    cmdList->IASetVertexBuffers(0, 1, &vbView);
    cmdList->IASetIndexBuffer(&ibView);
    cmdList->DrawIndexedInstanced(cubeMesh.indexCount, 1, 0, 0, 0);
}

void SimpleRenderer::RefreshGraphInteropTextures()
{
    m_DirectionalShadowGraphTexture.reset();
    m_LocalShadowGraphTexture.reset();
    m_SSAOOcclusionGraphTexture.reset();
    m_SSAOBlurredGraphTexture.reset();
    m_HZBGraphTexture.reset();

    if (!m_Device)
        return;

    if (m_ShadowDepthBuffer)
    {
        RHITextureDesc desc = {};
        desc.width = kShadowMapResolution;
        desc.height = kShadowMapResolution;
        desc.depth = 1;
        desc.mipLevels = 1;
        desc.arrayLayers = 1;
        desc.format = RHIFormat::D32_FLOAT;
        desc.type = RHITextureType::Texture2D;
        desc.usage = RHITextureUsage::DepthStencil | RHITextureUsage::Sampled;
        desc.debugName = "Shadow.Directional";
        m_DirectionalShadowGraphTexture =
            m_Device->WrapNativeTexture(m_ShadowDepthBuffer, desc, RHIResourceState::ShaderResource);
    }

    if (m_LocalShadowDepthBuffer)
    {
        RHITextureDesc desc = {};
        desc.width = kLocalShadowResolution;
        desc.height = kLocalShadowResolution;
        desc.depth = 1;
        desc.mipLevels = 1;
        desc.arrayLayers = kMaxLocalShadowSlices;
        desc.format = RHIFormat::D32_FLOAT;
        desc.type = RHITextureType::Texture2DArray;
        desc.usage = RHITextureUsage::DepthStencil | RHITextureUsage::Sampled;
        desc.debugName = "Shadow.LocalArray";
        m_LocalShadowGraphTexture =
            m_Device->WrapNativeTexture(m_LocalShadowDepthBuffer, desc, RHIResourceState::ShaderResource);
    }

    if (m_SSAOOcclusionRT && m_SSAOBufferWidth > 0 && m_SSAOBufferHeight > 0)
    {
        RHITextureDesc desc = {};
        desc.width = m_SSAOBufferWidth;
        desc.height = m_SSAOBufferHeight;
        desc.depth = 1;
        desc.mipLevels = 1;
        desc.arrayLayers = 1;
        desc.format = RHIFormat::R8_UNORM;
        desc.type = RHITextureType::Texture2D;
        desc.usage = RHITextureUsage::RenderTarget | RHITextureUsage::Sampled;
        desc.debugName = "SSAO.Occlusion";
        m_SSAOOcclusionGraphTexture =
            m_Device->WrapNativeTexture(m_SSAOOcclusionRT, desc, RHIResourceState::ShaderResource);
    }

    if (m_SSAOBlurredRT && m_SSAOBufferWidth > 0 && m_SSAOBufferHeight > 0)
    {
        RHITextureDesc desc = {};
        desc.width = m_SSAOBufferWidth;
        desc.height = m_SSAOBufferHeight;
        desc.depth = 1;
        desc.mipLevels = 1;
        desc.arrayLayers = 1;
        desc.format = RHIFormat::R8_UNORM;
        desc.type = RHITextureType::Texture2D;
        desc.usage = RHITextureUsage::RenderTarget | RHITextureUsage::Sampled;
        desc.debugName = "SSAO.Blurred";
        m_SSAOBlurredGraphTexture =
            m_Device->WrapNativeTexture(m_SSAOBlurredRT, desc, RHIResourceState::ShaderResource);
    }

    if (m_Occlusion.hzbTexture && m_Occlusion.width > 0 && m_Occlusion.height > 0)
    {
        RHITextureDesc desc = {};
        desc.width = m_Occlusion.width;
        desc.height = m_Occlusion.height;
        desc.depth = 1;
        desc.mipLevels = m_Occlusion.mipLevels;
        desc.arrayLayers = 1;
        desc.format = RHIFormat::R32_FLOAT;
        desc.type = RHITextureType::Texture2D;
        desc.usage = RHITextureUsage::Storage | RHITextureUsage::Sampled;
        desc.debugName = "HZB.Texture";
        m_HZBGraphTexture =
            m_Device->WrapNativeTexture(m_Occlusion.hzbTexture, desc, RHIResourceState::ShaderResource);
    }
}

ID3D12Device* SimpleRenderer::GetD3D12Device()
{
    return GetD3D12DevicePtr(m_Device);
}
} // namespace Dot

// Helper implementations to get D3D12 internals

ID3D12Device* GetD3D12DevicePtr(Dot::RHIDevice* device)
{
    return static_cast<Dot::D3D12Device*>(device)->GetDevice();
}

ID3D12GraphicsCommandList* GetD3D12CommandList(Dot::RHIDevice* device)
{
    return static_cast<Dot::D3D12Device*>(device)->GetCommandList();
}

ID3D12Resource* GetD3D12Buffer(Dot::RHIBuffer* buffer)
{
    return static_cast<Dot::D3D12Buffer*>(buffer)->GetResource();
}

D3D12_GPU_VIRTUAL_ADDRESS GetD3D12BufferGPUAddress(Dot::RHIBuffer* buffer)
{
    if (!buffer)
        return 0;
    ID3D12Resource* resource = static_cast<Dot::D3D12Buffer*>(buffer)->GetResource();
    if (!resource)
        return 0;
    return static_cast<Dot::D3D12Buffer*>(buffer)->GetGPUAddress();
}

