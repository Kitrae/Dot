// =============================================================================
// Dot Engine - RHI Unit Tests
// =============================================================================

#include "RHI/RHI.h"

#include <gtest/gtest.h>

using namespace Dot;

// =============================================================================
// RHITypes Tests
// =============================================================================

TEST(RHITypesTests, BufferDescDefaults)
{
    RHIBufferDesc desc;

    EXPECT_EQ(desc.size, 0);
    EXPECT_EQ(desc.usage, RHIBufferUsage::Vertex);
    EXPECT_EQ(desc.memory, RHIMemoryUsage::GPU_Only);
}

TEST(RHITypesTests, TextureDescDefaults)
{
    RHITextureDesc desc;

    EXPECT_EQ(desc.width, 1);
    EXPECT_EQ(desc.height, 1);
    EXPECT_EQ(desc.depth, 1);
    EXPECT_EQ(desc.mipLevels, 1);
    EXPECT_EQ(desc.format, RHIFormat::R8G8B8A8_UNORM);
    EXPECT_EQ(desc.type, RHITextureType::Texture2D);
}

TEST(RHITypesTests, SamplerDescDefaults)
{
    RHISamplerDesc desc;

    EXPECT_EQ(desc.minFilter, RHIFilter::Linear);
    EXPECT_EQ(desc.magFilter, RHIFilter::Linear);
    EXPECT_EQ(desc.addressU, RHISamplerAddressMode::Repeat);
    EXPECT_FLOAT_EQ(desc.maxAnisotropy, 1.0f);
}

TEST(RHITypesTests, BufferUsageFlags)
{
    RHIBufferUsage usage = RHIBufferUsage::Vertex | RHIBufferUsage::Index;

    EXPECT_EQ(static_cast<uint8>(usage),
              static_cast<uint8>(RHIBufferUsage::Vertex) | static_cast<uint8>(RHIBufferUsage::Index));
}

TEST(RHITypesTests, TextureUsageFlags)
{
    RHITextureUsage usage = RHITextureUsage::Sampled | RHITextureUsage::RenderTarget;

    EXPECT_EQ(static_cast<uint8>(usage),
              static_cast<uint8>(RHITextureUsage::Sampled) | static_cast<uint8>(RHITextureUsage::RenderTarget));
}

// =============================================================================
// ShaderBytecode Tests
// =============================================================================

TEST(RHIShaderTests, BytecodeDefaults)
{
    RHIShaderBytecode bytecode;

    EXPECT_TRUE(bytecode.data.empty());
    EXPECT_EQ(bytecode.stage, RHIShaderStage::Vertex);
    EXPECT_EQ(bytecode.entryPoint, "main");
}
