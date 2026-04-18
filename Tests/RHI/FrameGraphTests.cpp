// =============================================================================
// Dot Engine - Frame Graph Unit Tests
// =============================================================================

#include "RHI/FrameGraph.h"

#include <gtest/gtest.h>

using namespace Dot;

namespace
{

FrameGraphResourceDesc MakeColorTarget(const char* name, uint32 width = 1280, uint32 height = 720)
{
    return FrameGraphResourceDesc::Texture2D(
        name, width, height, RHIFormat::R8G8B8A8_UNORM, RHITextureUsage::RenderTarget | RHITextureUsage::Sampled);
}

FrameGraphResourceDesc MakeDepthTarget(const char* name, uint32 width = 1280, uint32 height = 720)
{
    return FrameGraphResourceDesc::Texture2D(
        name, width, height, RHIFormat::D32_FLOAT, RHITextureUsage::DepthStencil | RHITextureUsage::Sampled);
}

} // namespace

TEST(FrameGraphResourceTests, ImportedTextureDescriptorPreservesMetadata)
{
    auto desc = FrameGraphResourceDesc::ImportedTexture(
        "BackBuffer", 1920, 1080, RHIFormat::R8G8B8A8_UNORM, RHITextureUsage::RenderTarget, RHIResourceState::Present);

    EXPECT_EQ(desc.name, "BackBuffer");
    EXPECT_EQ(desc.type, FrameGraphResourceType::Texture);
    EXPECT_TRUE(desc.imported);
    EXPECT_EQ(desc.initialState, RHIResourceState::Present);
    EXPECT_EQ(desc.width, 1920u);
    EXPECT_EQ(desc.height, 1080u);
}

TEST(FrameGraphCompilationTests, WritingCreatesNewResourceVersionAndDependency)
{
    FrameGraph graph;
    FrameGraphTextureHandle sceneColor;
    FrameGraphTextureHandle lightingColor;

    graph.AddPass(
        "Scene",
        [&](FrameGraphPassBuilder& builder)
        {
            sceneColor = builder.CreateTexture(MakeColorTarget("SceneColor"));
        },
        [](RHIDevice&, const FrameGraphPass&) {});

    graph.AddPass(
        "Lighting",
        [&](FrameGraphPassBuilder& builder)
        {
            lightingColor = builder.Write(sceneColor, FrameGraphResourceUsage::ColorAttachment);
        },
        [](RHIDevice&, const FrameGraphPass&) {});

    graph.SetOutput(lightingColor);
    graph.Compile();

    ASSERT_EQ(graph.GetResourceCount(), 2u);
    const FrameGraphResource* original = graph.GetResource(sceneColor);
    const FrameGraphResource* written = graph.GetResource(lightingColor);
    ASSERT_NE(original, nullptr);
    ASSERT_NE(written, nullptr);

    EXPECT_EQ(original->logicalId, written->logicalId);
    EXPECT_EQ(original->version, 0u);
    EXPECT_EQ(written->version, 1u);
    EXPECT_EQ(written->parent, sceneColor);

    ASSERT_EQ(graph.GetPassCount(), 2u);
    EXPECT_EQ(graph.GetPasses()[1].dependencies.size(), 1u);
    EXPECT_EQ(graph.GetPasses()[1].dependencies[0], 0u);
}

TEST(FrameGraphCompilationTests, OutputsCullUnusedPassChains)
{
    FrameGraph graph;
    FrameGraphTextureHandle usedColor;
    FrameGraphTextureHandle unusedColor;

    graph.AddPass(
        "Used",
        [&](FrameGraphPassBuilder& builder)
        {
            usedColor = builder.CreateTexture(MakeColorTarget("UsedColor"));
        },
        [](RHIDevice&, const FrameGraphPass&) {});

    graph.AddPass(
        "Unused",
        [&](FrameGraphPassBuilder& builder)
        {
            unusedColor = builder.CreateTexture(MakeColorTarget("UnusedColor"));
        },
        [](RHIDevice&, const FrameGraphPass&) {});

    graph.SetOutput(usedColor);
    graph.Compile();

    ASSERT_EQ(graph.GetPassCount(), 2u);
    EXPECT_FALSE(graph.GetPasses()[0].culled);
    EXPECT_TRUE(graph.GetPasses()[1].culled);
    EXPECT_EQ(graph.GetActivePassCount(), 1u);
    (void)unusedColor;
}

TEST(FrameGraphCompilationTests, ImportedOutputsKeepProducerChainAlive)
{
    FrameGraph graph;
    const auto imported = graph.ImportTexture(
        FrameGraphResourceDesc::ImportedTexture("BackBuffer", 1280, 720, RHIFormat::R8G8B8A8_UNORM,
                                                RHITextureUsage::RenderTarget | RHITextureUsage::Sampled,
                                                RHIResourceState::Present));
    FrameGraphTextureHandle sceneColor;
    FrameGraphTextureHandle presentedColor;

    graph.AddPass(
        "Scene",
        [&](FrameGraphPassBuilder& builder)
        {
            sceneColor = builder.CreateTexture(MakeColorTarget("SceneColor"));
        },
        [](RHIDevice&, const FrameGraphPass&) {});

    graph.AddPass(
        "Present",
        [&](FrameGraphPassBuilder& builder)
        {
            builder.Read(sceneColor, FrameGraphResourceUsage::ShaderRead);
            presentedColor = builder.Write(imported, FrameGraphResourceUsage::Present);
        },
        [](RHIDevice&, const FrameGraphPass&) {});

    graph.SetOutput(presentedColor);
    graph.Compile();

    EXPECT_EQ(graph.GetActivePassCount(), 2u);
    EXPECT_FALSE(graph.GetPasses()[0].culled);
    EXPECT_FALSE(graph.GetPasses()[1].culled);
}

TEST(FrameGraphCompilationTests, WritingImportedResourceKeepsImportedPhysicalBacking)
{
    FrameGraph graph;
    const auto imported = graph.ImportTexture(
        FrameGraphResourceDesc::ImportedTexture("ImportedColor", 640, 480, RHIFormat::R8G8B8A8_UNORM,
                                                RHITextureUsage::RenderTarget | RHITextureUsage::Sampled,
                                                RHIResourceState::ShaderResource));
    FrameGraphTextureHandle written;

    graph.AddPass(
        "RewriteImported",
        [&](FrameGraphPassBuilder& builder)
        {
            written = builder.Write(imported, FrameGraphResourceUsage::ColorAttachment);
        },
        [](RHIDevice&, const FrameGraphPass&) {});

    graph.SetOutput(written);
    graph.Compile();

    const FrameGraphResource* original = graph.GetResource(imported);
    const FrameGraphResource* updated = graph.GetResource(written);
    ASSERT_NE(original, nullptr);
    ASSERT_NE(updated, nullptr);
    EXPECT_TRUE(updated->imported);
    EXPECT_EQ(original->physicalIndex, updated->physicalIndex);
}

TEST(FrameGraphCompilationTests, NonOverlappingTransientResourcesAliasPhysicalAllocations)
{
    FrameGraph graph;
    FrameGraphTextureHandle first;
    FrameGraphTextureHandle second;

    graph.AddPass(
        "First",
        [&](FrameGraphPassBuilder& builder)
        {
            first = builder.CreateTexture(MakeColorTarget("TransientA"));
        },
        [](RHIDevice&, const FrameGraphPass&) {});

    graph.AddPass(
        "Second",
        [&](FrameGraphPassBuilder& builder)
        {
            second = builder.CreateTexture(MakeColorTarget("TransientB"));
        },
        [](RHIDevice&, const FrameGraphPass&) {});

    graph.Compile();

    const FrameGraphResource* firstResource = graph.GetResource(first);
    const FrameGraphResource* secondResource = graph.GetResource(second);
    ASSERT_NE(firstResource, nullptr);
    ASSERT_NE(secondResource, nullptr);

    EXPECT_EQ(graph.GetPhysicalResourceCount(), 1u);
    EXPECT_EQ(firstResource->physicalIndex, secondResource->physicalIndex);
}

TEST(FrameGraphCompilationTests, OverlappingResourceLifetimesDoNotAlias)
{
    FrameGraph graph;
    FrameGraphTextureHandle sceneColor;
    FrameGraphTextureHandle lightingColor;
    FrameGraphTextureHandle postColor;

    graph.AddPass(
        "Scene",
        [&](FrameGraphPassBuilder& builder)
        {
            sceneColor = builder.CreateTexture(MakeColorTarget("SceneColor"));
        },
        [](RHIDevice&, const FrameGraphPass&) {});

    graph.AddPass(
        "Lighting",
        [&](FrameGraphPassBuilder& builder)
        {
            builder.Read(sceneColor, FrameGraphResourceUsage::ShaderRead);
            lightingColor = builder.CreateTexture(MakeColorTarget("LightingColor"));
        },
        [](RHIDevice&, const FrameGraphPass&) {});

    graph.AddPass(
        "Post",
        [&](FrameGraphPassBuilder& builder)
        {
            builder.Read(sceneColor, FrameGraphResourceUsage::ShaderRead);
            builder.Read(lightingColor, FrameGraphResourceUsage::ShaderRead);
            postColor = builder.CreateTexture(MakeColorTarget("PostColor"));
        },
        [](RHIDevice&, const FrameGraphPass&) {});

    graph.SetOutput(postColor);
    graph.Compile();

    const FrameGraphResource* sceneResource = graph.GetResource(sceneColor);
    const FrameGraphResource* lightingResource = graph.GetResource(lightingColor);
    ASSERT_NE(sceneResource, nullptr);
    ASSERT_NE(lightingResource, nullptr);

    EXPECT_NE(sceneResource->physicalIndex, lightingResource->physicalIndex);
}

TEST(FrameGraphCompilationTests, GeneratesStateTransitionsForDepthAndShaderRead)
{
    FrameGraph graph;
    FrameGraphTextureHandle sceneDepth;
    FrameGraphTextureHandle sceneColor;
    const FrameGraphTextureHandle backBuffer = graph.ImportTexture(
        FrameGraphResourceDesc::ImportedTexture("BackBuffer", 1280, 720, RHIFormat::R8G8B8A8_UNORM,
                                                RHITextureUsage::RenderTarget | RHITextureUsage::Sampled,
                                                RHIResourceState::Present));
    FrameGraphTextureHandle compositedBackBuffer;
    FrameGraphTextureHandle presentedBackBuffer;

    graph.AddPass(
        "DepthPrepass",
        [&](FrameGraphPassBuilder& builder)
        {
            sceneDepth = builder.CreateTexture(MakeDepthTarget("SceneDepth"));
        },
        [](RHIDevice&, const FrameGraphPass&) {});

    graph.AddPass(
        "MainScene",
        [&](FrameGraphPassBuilder& builder)
        {
            builder.Read(sceneDepth, FrameGraphResourceUsage::DepthStencilRead);
            sceneColor = builder.CreateTexture(MakeColorTarget("SceneColor"));
        },
        [](RHIDevice&, const FrameGraphPass&) {});

    graph.AddPass(
        "Present",
        [&](FrameGraphPassBuilder& builder)
        {
            builder.Read(sceneColor, FrameGraphResourceUsage::ShaderRead);
            compositedBackBuffer = builder.Write(backBuffer, FrameGraphResourceUsage::ColorAttachment);
            presentedBackBuffer = builder.Write(compositedBackBuffer, FrameGraphResourceUsage::Present);
        },
        [](RHIDevice&, const FrameGraphPass&) {});

    graph.SetOutput(presentedBackBuffer);
    graph.Compile();

    ASSERT_EQ(graph.GetPassCount(), 3u);
    ASSERT_EQ(graph.GetPasses()[1].barriers.size(), 1u);
    EXPECT_EQ(graph.GetPasses()[1].barriers[0].before, RHIResourceState::DepthWrite);
    EXPECT_EQ(graph.GetPasses()[1].barriers[0].after, RHIResourceState::DepthRead);

    bool sawSceneColorReadBarrier = false;
    bool sawPresentBarrier = false;
    for (const FrameGraphBarrier& barrier : graph.GetPasses()[2].barriers)
    {
        if (barrier.before == RHIResourceState::RenderTarget && barrier.after == RHIResourceState::ShaderResource)
            sawSceneColorReadBarrier = true;
        if (barrier.before == RHIResourceState::RenderTarget && barrier.after == RHIResourceState::Present)
            sawPresentBarrier = true;
    }
    EXPECT_TRUE(sawSceneColorReadBarrier);
    EXPECT_TRUE(sawPresentBarrier);
}

TEST(FrameGraphLifecycleTests, ResetClearsFrameStateButPreservesGraphObject)
{
    FrameGraph graph;
    graph.AddPass(
        "Test",
        [](FrameGraphPassBuilder& builder)
        {
            builder.CreateTexture(MakeColorTarget("RT", 800, 600));
        },
        [](RHIDevice&, const FrameGraphPass&) {});

    EXPECT_EQ(graph.GetPassCount(), 1u);

    graph.Reset();

    EXPECT_EQ(graph.GetPassCount(), 0u);
    EXPECT_EQ(graph.GetResourceCount(), 0u);
    EXPECT_EQ(graph.GetPhysicalResourceCount(), 0u);
    EXPECT_TRUE(graph.GetOutputs().empty());
    EXPECT_FALSE(graph.IsCompiled());
}

TEST(FrameGraphValidationTests, ReportsInvalidOutputHandle)
{
    FrameGraph graph;
    FrameGraphResourceHandle invalid;
    invalid.index = 999;

    graph.SetOutput(invalid);
    graph.Compile();

    EXPECT_TRUE(graph.HasValidationErrors());
}

TEST(FrameGraphValidationTests, ReportsConflictingStatesWithinSinglePass)
{
    FrameGraph graph;
    FrameGraphTextureHandle depth;

    graph.AddPass(
        "ConflictingDepthUse",
        [&](FrameGraphPassBuilder& builder)
        {
            depth = builder.CreateTexture(MakeDepthTarget("Depth"));
            builder.Read(depth, FrameGraphResourceUsage::DepthStencilRead);
            builder.Read(depth, FrameGraphResourceUsage::ShaderRead);
        },
        [](RHIDevice&, const FrameGraphPass&) {});

    graph.SetOutput(depth);
    graph.Compile();

    EXPECT_TRUE(graph.HasValidationErrors());
}
