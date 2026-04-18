#include "Core/ECS/World.h"
#include "Core/Map/MapCompiler.h"
#include "Core/Map/MapSerializer.h"
#include "Core/Map/MapTypes.h"
#include "Core/Map/StaticWorldGeometry.h"
#include "Core/Physics/BoxColliderComponent.h"
#include "Core/Physics/CharacterControllerComponent.h"
#include "Core/Physics/CharacterControllerSystem.h"
#include "Core/Reflect/Registry.h"
#include "Core/Scene/ComponentReflection.h"
#include "Core/Scene/LightComponent.h"
#include "Core/Scene/MapComponent.h"
#include "Core/Scene/Components.h"
#include "../../Editor/Source/Map/MapDocument.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>

using namespace Dot;

namespace
{

MapAsset CreateTestMapAsset()
{
    MapAsset asset;
    asset.nextBrushId = 8;
    asset.version = 2;

    MapBrush brush = CreateBoxBrush(7, Vec3(0.0f, 0.0f, 0.0f), Vec3(1.0f, 2.0f, 3.0f), "Assets/Materials/Base.dotmat");
    brush.name = "Test Brush";
    brush.bakedLighting.participateInBake = true;
    brush.bakedLighting.receiveBakedLighting = false;
    brush.bakedLighting.castBakedShadows = false;
    brush.bakedLighting.resolutionScale = 1.5f;
    brush.faces[0].materialPath = "Assets/Materials/Wall.dotmat";
    brush.faces[0].uv.projectionMode = MapProjectionMode::XZ;
    brush.faces[0].uv.scaleU = 2.0f;
    brush.faces[0].uv.scaleV = 3.0f;
    brush.faces[0].uv.offsetU = 0.25f;
    brush.faces[0].uv.offsetV = -0.5f;
    brush.faces[0].uv.rotationDeg = 45.0f;
    brush.faces[0].bakedLighting.bakeValid = true;
    brush.faces[0].bakedLighting.bakeStale = true;
    brush.faces[0].bakedLighting.useBakedLighting = false;
    brush.faces[0].bakedLighting.lightmapIntensity = 1.75f;
    brush.faces[0].bakedLighting.lightmapTexturePath = "Lightmaps/Test/lightmap_0.bmp";
    brush.faces[0].bakedLighting.lightmapSidecarPath = "Lightmaps/Test/Brush_7_Face_0.lightface.txt";
    brush.faces[0].bakedLighting.bakeSignature = "brush-sig";
    brush.faces[0].bakedLighting.lightmapScaleU = 0.5f;
    brush.faces[0].bakedLighting.lightmapScaleV = 0.25f;
    brush.faces[0].bakedLighting.lightmapOffsetU = 0.125f;
    brush.faces[0].bakedLighting.lightmapOffsetV = 0.375f;

    asset.brushes.push_back(std::move(brush));
    return asset;
}

MapAsset CreateCompiledMaterialMap()
{
    MapAsset asset;
    asset.nextBrushId = 2;

    MapBrush brush = CreateBoxBrush(1, Vec3(0.0f, 0.0f, 0.0f), Vec3(1.0f, 1.0f, 1.0f), "Assets/Materials/B.dotmat");
    brush.faces[0].materialPath = "Assets/Materials/A.dotmat";
    brush.faces[1].materialPath = "Assets/Materials/A.dotmat";
    asset.brushes.push_back(std::move(brush));
    return asset;
}

MapCompiledData CompileGroundPlane()
{
    MapAsset asset;
    asset.nextBrushId = 2;
    asset.brushes.push_back(CreateBoxBrush(1, Vec3(0.0f, -0.5f, 0.0f), Vec3(5.0f, 0.5f, 5.0f), "Assets/Materials/Ground.dotmat"));
    return MapCompiler::Compile(asset, 1);
}

MapCompiledData CompileRamp()
{
    MapAsset asset;
    asset.nextBrushId = 2;

    MapBrush brush;
    brush.brushId = 1;
    brush.name = "Ramp";
    brush.vertices = {
        {-1.0f, 0.0f, -2.0f},
        {1.0f, 0.0f, -2.0f},
        {1.0f, 0.4f, -2.0f},
        {-1.0f, 0.4f, -2.0f},
        {-1.0f, 0.0f, 2.0f},
        {1.0f, 0.0f, 2.0f},
        {1.0f, 2.0f, 2.0f},
        {-1.0f, 2.0f, 2.0f},
    };
    brush.faces = {
        {{0, 3, 2, 1}, "Assets/Materials/Rock.dotmat", {}},
        {{4, 5, 6, 7}, "Assets/Materials/Rock.dotmat", {}},
        {{0, 1, 5, 4}, "Assets/Materials/Rock.dotmat", {}},
        {{3, 7, 6, 2}, "Assets/Materials/Grass.dotmat", {}},
        {{0, 4, 7, 3}, "Assets/Materials/Rock.dotmat", {}},
        {{1, 2, 6, 5}, "Assets/Materials/Rock.dotmat", {}},
    };

    asset.brushes.push_back(std::move(brush));
    return MapCompiler::Compile(asset, 1);
}

MapBrush CreateRampBrush(uint32 brushId = 1)
{
    MapBrush brush;
    brush.brushId = brushId;
    brush.name = "Ramp";
    brush.vertices = {
        {-1.0f, 0.0f, -2.0f},
        {1.0f, 0.0f, -2.0f},
        {1.0f, 0.4f, -2.0f},
        {-1.0f, 0.4f, -2.0f},
        {-1.0f, 0.0f, 2.0f},
        {1.0f, 0.0f, 2.0f},
        {1.0f, 2.0f, 2.0f},
        {-1.0f, 2.0f, 2.0f},
    };
    brush.faces = {
        {{0, 3, 2, 1}, "Assets/Materials/Rock.dotmat", {}},
        {{4, 5, 6, 7}, "Assets/Materials/Rock.dotmat", {}},
        {{0, 1, 5, 4}, "Assets/Materials/Rock.dotmat", {}},
        {{3, 7, 6, 2}, "Assets/Materials/Grass.dotmat", {}},
        {{0, 4, 7, 3}, "Assets/Materials/Rock.dotmat", {}},
        {{1, 2, 6, 5}, "Assets/Materials/Rock.dotmat", {}},
    };
    return brush;
}

int FindFaceIndexByNormal(const MapBrush& brush, const Vec3& targetNormal)
{
    for (size_t faceIndex = 0; faceIndex < brush.faces.size(); ++faceIndex)
    {
        const Vec3 normal = ComputeMapFaceNormal(brush, brush.faces[faceIndex]);
        if (Vec3::Dot(normal, targetNormal) > 0.99f)
            return static_cast<int>(faceIndex);
    }
    return -1;
}

MapCompiledData CompileGroundWithLowStep()
{
    MapAsset asset;
    asset.nextBrushId = 3;
    asset.brushes.push_back(CreateBoxBrush(1, Vec3(0.0f, -0.5f, 0.0f), Vec3(5.0f, 0.5f, 5.0f), "Assets/Materials/Ground.dotmat"));
    asset.brushes.push_back(CreateBoxBrush(2, Vec3(0.0f, 0.1f, 0.75f), Vec3(1.0f, 0.1f, 0.75f), "Assets/Materials/Step.dotmat"));
    return MapCompiler::Compile(asset, 1);
}

MapCompiledData CompileGroundWithLongWall()
{
    MapAsset asset;
    asset.nextBrushId = 3;
    asset.brushes.push_back(CreateBoxBrush(1, Vec3(0.0f, -0.5f, 0.0f), Vec3(6.0f, 0.5f, 6.0f), "Assets/Materials/Ground.dotmat"));
    asset.brushes.push_back(CreateBoxBrush(2, Vec3(2.0f, 1.0f, 0.0f), Vec3(0.2f, 1.0f, 6.0f), "Assets/Materials/Wall.dotmat"));
    return MapCompiler::Compile(asset, 1);
}

MapCompiledData CompileRampIntoPlatform()
{
    MapAsset asset;
    asset.nextBrushId = 3;

    MapBrush ramp;
    ramp.brushId = 1;
    ramp.name = "Ramp";
    ramp.vertices = {
        {-1.0f, 0.0f, -2.0f},
        {1.0f, 0.0f, -2.0f},
        {1.0f, 0.25f, -2.0f},
        {-1.0f, 0.25f, -2.0f},
        {-1.0f, 0.0f, 2.0f},
        {1.0f, 0.0f, 2.0f},
        {1.0f, 1.5f, 2.0f},
        {-1.0f, 1.5f, 2.0f},
    };
    ramp.faces = {
        {{0, 3, 2, 1}, "Assets/Materials/Rock.dotmat", {}},
        {{4, 5, 6, 7}, "Assets/Materials/Rock.dotmat", {}},
        {{0, 1, 5, 4}, "Assets/Materials/Rock.dotmat", {}},
        {{3, 7, 6, 2}, "Assets/Materials/Grass.dotmat", {}},
        {{0, 4, 7, 3}, "Assets/Materials/Rock.dotmat", {}},
        {{1, 2, 6, 5}, "Assets/Materials/Rock.dotmat", {}},
    };
    asset.brushes.push_back(std::move(ramp));

    MapBrush platform = CreateBoxBrush(2, Vec3(0.0f, 0.75f, 3.0f), Vec3(1.5f, 0.75f, 1.0f), "Assets/Materials/Rock.dotmat");
    platform.faces[3].materialPath = "Assets/Materials/Grass.dotmat";
    asset.brushes.push_back(std::move(platform));

    return MapCompiler::Compile(asset, 1);
}

void ExpectVerticesEqual(const MapCompiledVertex& actual, const MapCompiledVertex& expected)
{
    EXPECT_FLOAT_EQ(actual.position.x, expected.position.x);
    EXPECT_FLOAT_EQ(actual.position.y, expected.position.y);
    EXPECT_FLOAT_EQ(actual.position.z, expected.position.z);
    EXPECT_FLOAT_EQ(actual.normal.x, expected.normal.x);
    EXPECT_FLOAT_EQ(actual.normal.y, expected.normal.y);
    EXPECT_FLOAT_EQ(actual.normal.z, expected.normal.z);
    EXPECT_FLOAT_EQ(actual.u, expected.u);
    EXPECT_FLOAT_EQ(actual.v, expected.v);
    EXPECT_FLOAT_EQ(actual.u2, expected.u2);
    EXPECT_FLOAT_EQ(actual.v2, expected.v2);
}

} // namespace

TEST(MapSerializerTests, RoundTripPreservesBrushTopologyMaterialsAndUvSettings)
{
    const std::filesystem::path tempPath = std::filesystem::temp_directory_path() / "dot_map_serializer_roundtrip.dotmap";
    MapSerializer serializer;
    MapAsset saved = CreateTestMapAsset();

    ASSERT_TRUE(serializer.Save(saved, tempPath.string())) << serializer.GetLastError();

    MapAsset loaded;
    ASSERT_TRUE(serializer.Load(loaded, tempPath.string())) << serializer.GetLastError();

    EXPECT_EQ(loaded.nextBrushId, saved.nextBrushId);
    ASSERT_EQ(loaded.brushes.size(), 1u);
    ASSERT_EQ(loaded.brushes[0].faces.size(), 6u);
    ASSERT_EQ(loaded.brushes[0].vertices.size(), 8u);
    EXPECT_EQ(loaded.brushes[0].brushId, 7u);
    EXPECT_EQ(loaded.brushes[0].name, "Test Brush");
    EXPECT_EQ(loaded.brushes[0].faces[0].materialPath, "Assets/Materials/Wall.dotmat");
    EXPECT_EQ(loaded.brushes[0].faces[0].uv.projectionMode, MapProjectionMode::XZ);
    EXPECT_FLOAT_EQ(loaded.brushes[0].faces[0].uv.scaleU, 2.0f);
    EXPECT_FLOAT_EQ(loaded.brushes[0].faces[0].uv.scaleV, 3.0f);
    EXPECT_FLOAT_EQ(loaded.brushes[0].faces[0].uv.offsetU, 0.25f);
    EXPECT_FLOAT_EQ(loaded.brushes[0].faces[0].uv.offsetV, -0.5f);
    EXPECT_FLOAT_EQ(loaded.brushes[0].faces[0].uv.rotationDeg, 45.0f);
    EXPECT_TRUE(loaded.brushes[0].bakedLighting.participateInBake);
    EXPECT_FALSE(loaded.brushes[0].bakedLighting.receiveBakedLighting);
    EXPECT_FALSE(loaded.brushes[0].bakedLighting.castBakedShadows);
    EXPECT_FLOAT_EQ(loaded.brushes[0].bakedLighting.resolutionScale, 1.5f);
    EXPECT_TRUE(loaded.brushes[0].faces[0].bakedLighting.bakeValid);
    EXPECT_TRUE(loaded.brushes[0].faces[0].bakedLighting.bakeStale);
    EXPECT_FALSE(loaded.brushes[0].faces[0].bakedLighting.useBakedLighting);
    EXPECT_FLOAT_EQ(loaded.brushes[0].faces[0].bakedLighting.lightmapIntensity, 1.75f);
    EXPECT_EQ(loaded.brushes[0].faces[0].bakedLighting.lightmapTexturePath, "Lightmaps/Test/lightmap_0.bmp");
    EXPECT_EQ(loaded.brushes[0].faces[0].bakedLighting.lightmapSidecarPath, "Lightmaps/Test/Brush_7_Face_0.lightface.txt");
    EXPECT_EQ(loaded.brushes[0].faces[0].bakedLighting.bakeSignature, "brush-sig");
    EXPECT_FLOAT_EQ(loaded.brushes[0].faces[0].bakedLighting.lightmapScaleU, 0.5f);
    EXPECT_FLOAT_EQ(loaded.brushes[0].faces[0].bakedLighting.lightmapScaleV, 0.25f);
    EXPECT_FLOAT_EQ(loaded.brushes[0].faces[0].bakedLighting.lightmapOffsetU, 0.125f);
    EXPECT_FLOAT_EQ(loaded.brushes[0].faces[0].bakedLighting.lightmapOffsetV, 0.375f);
    EXPECT_TRUE(ValidateMapBrushConvex(loaded.brushes[0]));

    std::filesystem::remove(tempPath);
}

TEST(MapCompilerTests, CompileIsDeterministicAndEmitsStablePerFaceSubmeshes)
{
    const MapAsset asset = CreateCompiledMaterialMap();

    const MapCompiledData first = MapCompiler::Compile(asset, 99);
    const MapCompiledData second = MapCompiler::Compile(asset, 99);

    ASSERT_EQ(first.submeshes.size(), asset.brushes[0].faces.size());
    EXPECT_EQ(first.indices.size(), second.indices.size());
    EXPECT_EQ(first.vertices.size(), second.vertices.size());
    EXPECT_EQ(first.submeshes.size(), second.submeshes.size());
    EXPECT_EQ(first.collisionTriangles.size(), 12u);
    EXPECT_EQ(first.boundsMin.x, -1.0f);
    EXPECT_EQ(first.boundsMax.z, 1.0f);

    int aMaterialCount = 0;
    int bMaterialCount = 0;
    for (size_t i = 0; i < first.submeshes.size(); ++i)
    {
        EXPECT_EQ(first.submeshes[i].indexStart, second.submeshes[i].indexStart);
        EXPECT_EQ(first.submeshes[i].indexCount, second.submeshes[i].indexCount);
        EXPECT_EQ(first.submeshes[i].materialPath, second.submeshes[i].materialPath);
        EXPECT_EQ(first.submeshes[i].brushId, 1u);
        EXPECT_EQ(first.submeshes[i].faceIndex, static_cast<uint32>(i));
        EXPECT_GT(first.submeshes[i].indexCount, 0u);

        if (first.submeshes[i].materialPath == "Assets/Materials/A.dotmat")
            ++aMaterialCount;
        if (first.submeshes[i].materialPath == "Assets/Materials/B.dotmat")
            ++bMaterialCount;
    }

    EXPECT_EQ(aMaterialCount, 2);
    EXPECT_EQ(bMaterialCount, 4);

    for (size_t i = 0; i < first.vertices.size(); ++i)
        ExpectVerticesEqual(first.vertices[i], second.vertices[i]);
    for (size_t i = 0; i < first.indices.size(); ++i)
        EXPECT_EQ(first.indices[i], second.indices[i]);
}

TEST(MapCompilerTests, SlopedBrushTopFaceCompilesWithUpwardNormal)
{
    const MapCompiledData ramp = CompileRamp();

    bool foundWalkableTriangle = false;
    for (const MapCompiledTriangle& triangle : ramp.collisionTriangles)
    {
        if (triangle.normal.y > 0.5f)
        {
            foundWalkableTriangle = true;
            EXPECT_GT(triangle.normal.y, 0.0f);
        }
    }

    EXPECT_TRUE(foundWalkableTriangle);
}


TEST(MapDocumentTests, PasteBrushCreatesNewSelectedBrushWithOffset)
{
    MapDocument document;
    document.New();

    const uint32 originalId = document.CreateBoxBrush(Vec3(0.0f, 0.0f, 0.0f), Vec3(1.0f, 1.0f, 1.0f));
    const MapBrush* originalBrush = document.GetSelectedBrush();
    ASSERT_NE(originalBrush, nullptr);
    const MapBrush originalBrushCopy = *originalBrush;

    const Vec3 originalFirstVertex = originalBrushCopy.vertices.front();
    const uint32 pastedId = document.PasteBrush(originalBrushCopy, Vec3(2.0f, 0.0f, 0.0f), true);

    EXPECT_NE(pastedId, 0u);
    EXPECT_NE(pastedId, originalId);
    ASSERT_EQ(document.GetAsset().brushes.size(), 2u);
    EXPECT_EQ(document.GetSelection().brushId, pastedId);

    const MapBrush* pastedBrush = document.GetSelectedBrush();
    ASSERT_NE(pastedBrush, nullptr);
    EXPECT_EQ(pastedBrush->name, originalBrushCopy.name + " Copy");
    EXPECT_EQ(pastedBrush->vertices.front(), originalFirstVertex + Vec3(2.0f, 0.0f, 0.0f));
}

TEST(MapDocumentTests, PasteBrushCanPreserveNameForCutPaste)
{
    MapDocument document;
    document.New();

    document.CreateBoxBrush(Vec3(0.0f, 0.0f, 0.0f), Vec3(1.0f, 1.0f, 1.0f));
    MapBrush originalBrush = *document.GetSelectedBrush();
    originalBrush.name = "Hallway";

    const uint32 pastedId = document.PasteBrush(originalBrush, Vec3(1.0f, 0.0f, 0.0f), false);
    ASSERT_NE(pastedId, 0u);

    const MapBrush* pastedBrush = document.GetSelectedBrush();
    ASSERT_NE(pastedBrush, nullptr);
    EXPECT_EQ(pastedBrush->name, "Hallway");
}

TEST(MapDocumentTests, ClipSelectedBrushCutsBoxAlongSelectedFacePlane)
{
    MapDocument document;
    document.New();

    document.CreateBoxBrush(Vec3(0.0f, 0.0f, 0.0f), Vec3(1.0f, 1.0f, 1.0f));
    MapSelection selection;
    selection.brushId = document.GetSelection().brushId;
    selection.faceIndex = 5;
    document.SetSelection(selection);

    ASSERT_TRUE(document.ClipSelectedBrush(-0.5f, false));

    const MapBrush* clippedBrush = document.GetSelectedBrush();
    ASSERT_NE(clippedBrush, nullptr);
    EXPECT_TRUE(ValidateMapBrushConvex(*clippedBrush));
    EXPECT_EQ(document.GetSelectionMode(), MapSelectionMode::Face);
    EXPECT_GE(document.GetSelection().faceIndex, 0);

    float minX = clippedBrush->vertices.front().x;
    float maxX = clippedBrush->vertices.front().x;
    for (const Vec3& vertex : clippedBrush->vertices)
    {
        minX = std::min(minX, vertex.x);
        maxX = std::max(maxX, vertex.x);
    }

    EXPECT_NEAR(minX, -1.0f, 0.001f);
    EXPECT_NEAR(maxX, 0.5f, 0.001f);
}

TEST(MapDocumentTests, ClipSelectedBrushCanKeepOppositeHalf)
{
    MapDocument document;
    document.New();

    document.CreateBoxBrush(Vec3(0.0f, 0.0f, 0.0f), Vec3(1.0f, 1.0f, 1.0f));
    MapSelection selection;
    selection.brushId = document.GetSelection().brushId;
    selection.faceIndex = 5;
    document.SetSelection(selection);

    ASSERT_TRUE(document.ClipSelectedBrush(-0.5f, true));

    const MapBrush* clippedBrush = document.GetSelectedBrush();
    ASSERT_NE(clippedBrush, nullptr);
    EXPECT_TRUE(ValidateMapBrushConvex(*clippedBrush));

    float minX = clippedBrush->vertices.front().x;
    float maxX = clippedBrush->vertices.front().x;
    for (const Vec3& vertex : clippedBrush->vertices)
    {
        minX = std::min(minX, vertex.x);
        maxX = std::max(maxX, vertex.x);
    }

    EXPECT_NEAR(minX, 0.5f, 0.001f);
    EXPECT_NEAR(maxX, 1.0f, 0.001f);
}

TEST(MapDocumentTests, ExtrudeSelectedFaceExtendsBoxAndKeepsBrushConvex)
{
    MapDocument document;
    document.New();

    document.CreateBoxBrush(Vec3(0.0f, 0.0f, 0.0f), Vec3(1.0f, 1.0f, 1.0f));
    MapSelection selection;
    selection.brushId = document.GetSelection().brushId;
    selection.faceIndex = 5;
    document.SetSelection(selection);

    ASSERT_TRUE(document.ExtrudeSelectedFace(0.5f));

    const MapBrush* extrudedBrush = document.GetSelectedBrush();
    ASSERT_NE(extrudedBrush, nullptr);
    EXPECT_TRUE(ValidateMapBrushConvex(*extrudedBrush));
    EXPECT_EQ(document.GetSelectionMode(), MapSelectionMode::Face);
    EXPECT_EQ(extrudedBrush->faces.size(), 6u);

    float minX = extrudedBrush->vertices.front().x;
    float maxX = extrudedBrush->vertices.front().x;
    for (const Vec3& vertex : extrudedBrush->vertices)
    {
        minX = std::min(minX, vertex.x);
        maxX = std::max(maxX, vertex.x);
    }

    EXPECT_NEAR(minX, -1.0f, 0.001f);
    EXPECT_NEAR(maxX, 1.5f, 0.001f);
}

TEST(MapDocumentTests, ExtrudeSelectedFaceSupportsSlopedConvexBrushFaces)
{
    MapDocument document;
    document.New();
    document.GetAsset().nextBrushId = 2;
    document.GetAsset().brushes.push_back(CreateRampBrush(1));
    document.RebuildCompiledData();

    MapSelection selection;
    selection.brushId = 1;
    selection.faceIndex = 3;
    document.SetSelection(selection);

    const MapBrush* originalBrush = document.GetSelectedBrush();
    const MapFace* originalFace = document.GetSelectedFace();
    ASSERT_NE(originalBrush, nullptr);
    ASSERT_NE(originalFace, nullptr);
    ASSERT_TRUE(ValidateMapBrushConvex(*originalBrush));

    const Vec3 originalNormal = ComputeMapFaceNormal(*originalBrush, *originalFace);
    const float originalPlaneDistance = Vec3::Dot(originalNormal, originalBrush->vertices[originalFace->vertexIndices.front()]);

    std::vector<Vec3> previewPolygon;
    Vec3 previewNormal = Vec3::Zero();
    ASSERT_TRUE(document.BuildSelectedFaceExtrudePreview(0.5f, previewPolygon, previewNormal));
    EXPECT_GT(previewPolygon.size(), 2u);
    EXPECT_GT(Vec3::Dot(originalNormal, previewNormal), 0.99f);

    ASSERT_TRUE(document.ExtrudeSelectedFace(0.5f));

    const MapBrush* extrudedBrush = document.GetSelectedBrush();
    const MapFace* extrudedFace = document.GetSelectedFace();
    ASSERT_NE(extrudedBrush, nullptr);
    ASSERT_NE(extrudedFace, nullptr);
    EXPECT_TRUE(ValidateMapBrushConvex(*extrudedBrush));
    EXPECT_EQ(extrudedBrush->faces.size(), 6u);

    const Vec3 extrudedNormal = ComputeMapFaceNormal(*extrudedBrush, *extrudedFace);
    const float extrudedPlaneDistance =
        Vec3::Dot(originalNormal, extrudedBrush->vertices[extrudedFace->vertexIndices.front()]);
    EXPECT_GT(Vec3::Dot(originalNormal, extrudedNormal), 0.99f);
    EXPECT_NEAR(extrudedPlaneDistance - originalPlaneDistance, 0.5f, 0.02f);
}

TEST(MapDocumentTests, HollowSelectedBrushReplacesBoxWithSixShellBrushes)
{
    MapDocument document;
    document.New();

    const uint32 originalBrushId = document.CreateBoxBrush(Vec3(0.0f, 0.0f, 0.0f), Vec3(2.0f, 1.5f, 3.0f));
    MapSelection selection;
    selection.brushId = originalBrushId;
    document.SetSelection(selection);

    ASSERT_TRUE(document.HollowSelectedBrush(0.5f));

    const MapAsset& asset = document.GetAsset();
    ASSERT_EQ(asset.brushes.size(), 6u);
    EXPECT_EQ(document.GetSelectionMode(), MapSelectionMode::Brush);
    EXPECT_EQ(document.GetSelection().brushId, originalBrushId);

    for (const MapBrush& brush : asset.brushes)
        EXPECT_TRUE(ValidateMapBrushConvex(brush));

    Vec3 overallMin(9999.0f, 9999.0f, 9999.0f);
    Vec3 overallMax(-9999.0f, -9999.0f, -9999.0f);
    for (const MapBrush& brush : asset.brushes)
    {
        for (const Vec3& vertex : brush.vertices)
        {
            overallMin.x = std::min(overallMin.x, vertex.x);
            overallMin.y = std::min(overallMin.y, vertex.y);
            overallMin.z = std::min(overallMin.z, vertex.z);
            overallMax.x = std::max(overallMax.x, vertex.x);
            overallMax.y = std::max(overallMax.y, vertex.y);
            overallMax.z = std::max(overallMax.z, vertex.z);
        }
    }

    EXPECT_NEAR(overallMin.x, -2.0f, 0.001f);
    EXPECT_NEAR(overallMin.y, -1.5f, 0.001f);
    EXPECT_NEAR(overallMin.z, -3.0f, 0.001f);
    EXPECT_NEAR(overallMax.x, 2.0f, 0.001f);
    EXPECT_NEAR(overallMax.y, 1.5f, 0.001f);
    EXPECT_NEAR(overallMax.z, 3.0f, 0.001f);
}

TEST(MapDocumentTests, HollowSelectedBrushSupportsSlopedConvexBrushes)
{
    MapDocument document;
    document.New();
    document.GetAsset().nextBrushId = 2;
    document.GetAsset().brushes.push_back(CreateRampBrush(1));
    document.RebuildCompiledData();

    MapSelection selection;
    selection.brushId = 1;
    document.SetSelection(selection);

    ASSERT_TRUE(document.HollowSelectedBrush(0.2f));

    const MapAsset& asset = document.GetAsset();
    ASSERT_EQ(asset.brushes.size(), 6u);
    EXPECT_EQ(document.GetSelectionMode(), MapSelectionMode::Brush);
    EXPECT_EQ(document.GetSelection().brushId, 1u);

    bool foundGrassFace = false;
    for (const MapBrush& brush : asset.brushes)
    {
        EXPECT_TRUE(ValidateMapBrushConvex(brush));
        for (const MapFace& face : brush.faces)
        {
            if (face.materialPath == "Assets/Materials/Grass.dotmat")
                foundGrassFace = true;
        }
    }

    EXPECT_TRUE(foundGrassFace);
}

TEST(MapDocumentTests, TextureWorkflowCanApplyBrushMaterialAndFaceUvOperations)
{
    MapDocument document;
    document.New();
    const uint32 brushId = document.CreateBoxBrush(Vec3(0.0f, 0.0f, 0.0f), Vec3(2.0f, 1.0f, 3.0f), "Assets/Materials/Base.dotmat");

    MapSelection selection;
    selection.brushId = brushId;
    selection.faceIndex = 0;
    document.SetSelection(selection);

    ASSERT_TRUE(document.SetSelectedBrushMaterial("Assets/Materials/Wall.dotmat"));
    const MapBrush* brush = document.GetSelectedBrush();
    ASSERT_NE(brush, nullptr);
    for (const MapFace& face : brush->faces)
        EXPECT_EQ(face.materialPath, "Assets/Materials/Wall.dotmat");

    ASSERT_TRUE(document.FitSelectedFaceUV());
    const MapFace* face = document.GetSelectedFace();
    ASSERT_NE(face, nullptr);
    EXPECT_GT(std::abs(face->uv.scaleU), 0.0f);
    EXPECT_GT(std::abs(face->uv.scaleV), 0.0f);

    const float fittedOffsetU = face->uv.offsetU;
    const float fittedOffsetV = face->uv.offsetV;
    const float fittedRotation = face->uv.rotationDeg;
    ASSERT_TRUE(document.NudgeSelectedFaceUV(0.5f, -0.25f));
    face = document.GetSelectedFace();
    EXPECT_FLOAT_EQ(face->uv.offsetU, fittedOffsetU + 0.5f);
    EXPECT_FLOAT_EQ(face->uv.offsetV, fittedOffsetV - 0.25f);

    ASSERT_TRUE(document.RotateSelectedFaceUV(90.0f));
    face = document.GetSelectedFace();
    EXPECT_FLOAT_EQ(face->uv.rotationDeg, fittedRotation + 90.0f);

    const float fittedScaleU = face->uv.scaleU;
    ASSERT_TRUE(document.FlipSelectedFaceUV(true, false));
    face = document.GetSelectedFace();
    EXPECT_FLOAT_EQ(face->uv.scaleU, -fittedScaleU);
}

TEST(MapDocumentTests, MultiFaceSelectionAppliesSurfaceOperationsAcrossBrushes)
{
    MapDocument document;
    document.New();
    const uint32 firstBrushId =
        document.CreateBoxBrush(Vec3(0.0f, 0.0f, 0.0f), Vec3(1.0f, 1.0f, 1.0f), "Assets/Materials/Base.dotmat");
    const uint32 secondBrushId =
        document.CreateBoxBrush(Vec3(4.0f, 0.0f, 0.0f), Vec3(1.0f, 1.0f, 1.0f), "Assets/Materials/Base.dotmat");

    std::vector<MapSelection> selections;
    selections.push_back(MapSelection{firstBrushId, 0});
    selections.push_back(MapSelection{secondBrushId, 0});
    document.SetSelections(selections);

    ASSERT_TRUE(document.SetSelectedFaceMaterial("Assets/Materials/Trim.dotmat"));
    ASSERT_TRUE(document.NudgeSelectedFaceUV(0.5f, -0.25f));

    const MapBrush* firstBrush = document.GetAsset().FindBrush(firstBrushId);
    const MapBrush* secondBrush = document.GetAsset().FindBrush(secondBrushId);
    ASSERT_NE(firstBrush, nullptr);
    ASSERT_NE(secondBrush, nullptr);

    EXPECT_EQ(firstBrush->faces[0].materialPath, "Assets/Materials/Trim.dotmat");
    EXPECT_EQ(secondBrush->faces[0].materialPath, "Assets/Materials/Trim.dotmat");
    EXPECT_FLOAT_EQ(firstBrush->faces[0].uv.offsetU, secondBrush->faces[0].uv.offsetU);
    EXPECT_FLOAT_EQ(firstBrush->faces[0].uv.offsetV, secondBrush->faces[0].uv.offsetV);
}

TEST(MapDocumentTests, MultiVertexSelectionTranslatesSelectedVerticesAcrossBrushes)
{
    MapDocument document;
    document.New();
    const uint32 firstBrushId = document.CreateBoxBrush(Vec3(0.0f, 0.0f, 0.0f), Vec3(1.0f, 1.0f, 1.0f));
    const uint32 secondBrushId = document.CreateBoxBrush(Vec3(4.0f, 0.0f, 0.0f), Vec3(1.0f, 1.0f, 1.0f));

    std::vector<MapSelection> selections;
    MapSelection firstVertex;
    firstVertex.brushId = firstBrushId;
    firstVertex.vertexIndex = 0;
    selections.push_back(firstVertex);
    MapSelection secondVertex;
    secondVertex.brushId = secondBrushId;
    secondVertex.vertexIndex = 0;
    selections.push_back(secondVertex);
    document.SetSelections(selections);

    const Vec3 firstBefore = document.GetAsset().FindBrush(firstBrushId)->vertices[0];
    const Vec3 secondBefore = document.GetAsset().FindBrush(secondBrushId)->vertices[0];

    ASSERT_TRUE(document.TranslateSelectedVertex(Vec3(0.25f, 0.0f, 0.0f), false));

    const MapBrush* firstBrush = document.GetAsset().FindBrush(firstBrushId);
    const MapBrush* secondBrush = document.GetAsset().FindBrush(secondBrushId);
    ASSERT_NE(firstBrush, nullptr);
    ASSERT_NE(secondBrush, nullptr);

    EXPECT_NEAR(firstBrush->vertices[0].x, firstBefore.x + 0.25f, 0.0001f);
    EXPECT_NEAR(secondBrush->vertices[0].x, secondBefore.x + 0.25f, 0.0001f);
}

TEST(MapDocumentTests, SelectAllCoplanarFacesFindsMatchingPlaneAcrossBrushes)
{
    MapDocument document;
    document.New();
    const uint32 firstBrushId = document.CreateBoxBrush(Vec3(0.0f, 0.0f, 0.0f), Vec3(1.0f, 1.0f, 1.0f));
    const uint32 secondBrushId = document.CreateBoxBrush(Vec3(4.0f, 0.0f, 0.0f), Vec3(1.0f, 1.0f, 1.0f));

    const MapBrush* firstBrush = document.GetAsset().FindBrush(firstBrushId);
    const MapBrush* secondBrush = document.GetAsset().FindBrush(secondBrushId);
    ASSERT_NE(firstBrush, nullptr);
    ASSERT_NE(secondBrush, nullptr);

    const int firstTopFace = FindFaceIndexByNormal(*firstBrush, Vec3::UnitY());
    const int secondTopFace = FindFaceIndexByNormal(*secondBrush, Vec3::UnitY());
    ASSERT_GE(firstTopFace, 0);
    ASSERT_GE(secondTopFace, 0);

    MapSelection selection;
    selection.brushId = firstBrushId;
    selection.faceIndex = firstTopFace;
    document.SetSelection(selection);

    ASSERT_TRUE(document.SelectAllCoplanarFaces());
    const std::vector<MapSelection>& selections = document.GetSelections();
    EXPECT_EQ(selections.size(), 2u);
    EXPECT_TRUE(document.IsSelectionSelected(MapSelection{firstBrushId, firstTopFace}));
    EXPECT_TRUE(document.IsSelectionSelected(MapSelection{secondBrushId, secondTopFace}));
}

TEST(MapDocumentTests, SelectFacesWithSameMaterialCollectsMatchingFacesAcrossBrushes)
{
    MapDocument document;
    document.New();
    const uint32 firstBrushId =
        document.CreateBoxBrush(Vec3(0.0f, 0.0f, 0.0f), Vec3(1.0f, 1.0f, 1.0f), "Assets/Materials/Base.dotmat");
    const uint32 secondBrushId =
        document.CreateBoxBrush(Vec3(4.0f, 0.0f, 0.0f), Vec3(1.0f, 1.0f, 1.0f), "Assets/Materials/Base.dotmat");

    MapBrush* firstBrush = document.GetAsset().FindBrush(firstBrushId);
    MapBrush* secondBrush = document.GetAsset().FindBrush(secondBrushId);
    ASSERT_NE(firstBrush, nullptr);
    ASSERT_NE(secondBrush, nullptr);

    const int firstTopFace = FindFaceIndexByNormal(*firstBrush, Vec3::UnitY());
    const int secondTopFace = FindFaceIndexByNormal(*secondBrush, Vec3::UnitY());
    ASSERT_GE(firstTopFace, 0);
    ASSERT_GE(secondTopFace, 0);

    firstBrush->faces[static_cast<size_t>(firstTopFace)].materialPath = "Assets/Materials/Trim.dotmat";
    secondBrush->faces[static_cast<size_t>(secondTopFace)].materialPath = "Assets/Materials/Trim.dotmat";

    MapSelection selection;
    selection.brushId = firstBrushId;
    selection.faceIndex = firstTopFace;
    document.SetSelection(selection);

    ASSERT_TRUE(document.SelectFacesWithSameMaterial());
    EXPECT_EQ(document.GetSelections().size(), 2u);
    EXPECT_TRUE(document.IsSelectionSelected(MapSelection{firstBrushId, firstTopFace}));
    EXPECT_TRUE(document.IsSelectionSelected(MapSelection{secondBrushId, secondTopFace}));
}

TEST(MapDocumentTests, SelectLinkedBrushFacesCollectsEntireBrush)
{
    MapDocument document;
    document.New();
    const uint32 brushId = document.CreateBoxBrush(Vec3(0.0f, 0.0f, 0.0f), Vec3(1.0f, 1.0f, 1.0f));

    MapSelection selection;
    selection.brushId = brushId;
    document.SetSelection(selection);

    ASSERT_TRUE(document.SelectLinkedBrushFaces());
    EXPECT_EQ(document.GetSelections().size(), 6u);
    for (int faceIndex = 0; faceIndex < 6; ++faceIndex)
        EXPECT_TRUE(document.IsSelectionSelected(MapSelection{brushId, faceIndex}));
}

TEST(MapDocumentTests, GrowAndShrinkFaceSelectionExpandAndContractAdjacentFaces)
{
    MapDocument document;
    document.New();
    const uint32 brushId = document.CreateBoxBrush(Vec3(0.0f, 0.0f, 0.0f), Vec3(1.0f, 1.0f, 1.0f));

    const MapBrush* brush = document.GetAsset().FindBrush(brushId);
    ASSERT_NE(brush, nullptr);
    const int topFace = FindFaceIndexByNormal(*brush, Vec3::UnitY());
    ASSERT_GE(topFace, 0);

    MapSelection selection;
    selection.brushId = brushId;
    selection.faceIndex = topFace;
    document.SetSelection(selection);

    ASSERT_TRUE(document.GrowFaceSelection());
    EXPECT_EQ(document.GetSelections().size(), 5u);
    EXPECT_TRUE(document.IsSelectionSelected(MapSelection{brushId, topFace}));

    ASSERT_TRUE(document.ShrinkFaceSelection());
    EXPECT_EQ(document.GetSelections().size(), 1u);
    EXPECT_TRUE(document.IsSelectionSelected(MapSelection{brushId, topFace}));
}

TEST(MapDocumentTests, HideLockAndIsolateBrushesUpdateVisibilityAndEditingState)
{
    auto computeCenter = [](const MapBrush& brush)
    {
        Vec3 center = Vec3::Zero();
        for (const Vec3& vertex : brush.vertices)
            center += vertex;
        return brush.vertices.empty() ? Vec3::Zero() : (center / static_cast<float>(brush.vertices.size()));
    };

    MapDocument document;
    document.New();

    const uint32 firstBrushId = document.CreateBoxBrush(Vec3(0.0f, 0.0f, 0.0f), Vec3(1.0f, 1.0f, 1.0f));
    const uint32 secondBrushId = document.CreateBoxBrush(Vec3(4.0f, 0.0f, 0.0f), Vec3(1.0f, 1.0f, 1.0f));
    const uint32 thirdBrushId = document.CreateBoxBrush(Vec3(8.0f, 0.0f, 0.0f), Vec3(1.0f, 1.0f, 1.0f));

    document.SetSelections({MapSelection{firstBrushId}, MapSelection{secondBrushId}});

    ASSERT_TRUE(document.HideSelectedBrushes());
    EXPECT_TRUE(document.IsBrushHidden(firstBrushId));
    EXPECT_TRUE(document.IsBrushHidden(secondBrushId));
    EXPECT_FALSE(document.IsBrushHidden(thirdBrushId));
    EXPECT_TRUE(document.GetSelections().empty());

    ASSERT_TRUE(document.UnhideAllBrushes());
    EXPECT_FALSE(document.IsBrushHidden(firstBrushId));
    EXPECT_FALSE(document.IsBrushHidden(secondBrushId));

    document.SetSelections({MapSelection{secondBrushId}});
    ASSERT_TRUE(document.IsolateSelectedBrushes());
    EXPECT_TRUE(document.IsBrushHidden(firstBrushId));
    EXPECT_FALSE(document.IsBrushHidden(secondBrushId));
    EXPECT_TRUE(document.IsBrushHidden(thirdBrushId));

    ASSERT_TRUE(document.LockSelectedBrushes());
    EXPECT_TRUE(document.IsBrushLocked(secondBrushId));

    const MapBrush* lockedBrushBefore = document.GetAsset().FindBrush(secondBrushId);
    ASSERT_NE(lockedBrushBefore, nullptr);
    const Vec3 beforeCenter = computeCenter(*lockedBrushBefore);

    EXPECT_FALSE(document.TranslateSelectedBrush(Vec3(1.0f, 0.0f, 0.0f)));

    const MapBrush* lockedBrushAfter = document.GetAsset().FindBrush(secondBrushId);
    ASSERT_NE(lockedBrushAfter, nullptr);
    EXPECT_TRUE(computeCenter(*lockedBrushAfter).ApproxEqual(beforeCenter, 0.001f));

    ASSERT_TRUE(document.UnlockAllBrushes());
    EXPECT_FALSE(document.IsBrushLocked(secondBrushId));
}

TEST(StaticWorldGeometryTests, RaycastAndOverlapUseCompiledTriangles)
{
    StaticWorldGeometry staticWorld;
    staticWorld.Build(CompileGroundPlane());

    Ray ray;
    ray.origin = Vec3(0.0f, 2.0f, 0.0f);
    ray.direction = Vec3(0.0f, -1.0f, 0.0f);

    StaticWorldHit hit;
    ASSERT_TRUE(staticWorld.Raycast(ray, 10.0f, hit));
    EXPECT_NEAR(hit.point.y, 0.0f, 0.001f);
    EXPECT_NEAR(hit.normal.y, 1.0f, 0.001f);
    EXPECT_EQ(hit.brushId, 1u);

    StaticWorldHit overlapHit;
    EXPECT_TRUE(staticWorld.OverlapSphere(Vec3(0.0f, 0.1f, 0.0f), 0.2f, overlapHit));
    EXPECT_FALSE(staticWorld.OverlapSphere(Vec3(0.0f, 3.0f, 0.0f), 0.2f, overlapHit));
}

TEST(CharacterControllerStaticWorldTests, GroundCheckUsesCompiledMapGeometry)
{
    World world;
    CharacterControllerSystem controllerSystem;
    StaticWorldGeometry staticWorld;
    staticWorld.Build(CompileGroundPlane());
    controllerSystem.SetStaticWorldGeometry(&staticWorld);

    Entity controller = world.CreateEntity();
    auto& transform = world.AddComponent<TransformComponent>(controller);
    transform.position = Vec3(0.0f, 1.05f, 0.0f);
    transform.scale = Vec3(1.0f, 1.0f, 1.0f);

    auto& box = world.AddComponent<BoxColliderComponent>(controller);
    box.size = Vec3(1.0f, 2.0f, 1.0f);

    auto& character = world.AddComponent<CharacterControllerComponent>(controller);
    character.groundCheckDistance = 0.1f;
    character.skinWidth = 0.02f;
    character.useGravity = false;

    controllerSystem.Update(world, 1.0f / 60.0f);

    EXPECT_TRUE(controllerSystem.IsGrounded(world, controller));
    EXPECT_NEAR(character.groundNormal.y, 1.0f, 0.001f);
}

TEST(CharacterControllerStaticWorldTests, ControllerClimbsRampWithoutSinkingIntoStaticWorld)
{
    World world;
    CharacterControllerSystem controllerSystem;
    StaticWorldGeometry staticWorld;
    staticWorld.Build(CompileRamp());
    controllerSystem.SetStaticWorldGeometry(&staticWorld);

    Entity controller = world.CreateEntity();
    auto& transform = world.AddComponent<TransformComponent>(controller);
    transform.position = Vec3(0.0f, 2.1f, -0.5f);
    transform.scale = Vec3(1.0f, 1.0f, 1.0f);

    auto& box = world.AddComponent<BoxColliderComponent>(controller);
    box.size = Vec3(1.0f, 2.0f, 1.0f);

    auto& character = world.AddComponent<CharacterControllerComponent>(controller);
    character.moveSpeed = 3.5f;
    character.groundCheckDistance = 0.15f;
    character.skinWidth = 0.02f;
    character.useGravity = false;

    const float dt = 1.0f / 60.0f;
    for (int step = 0; step < 40; ++step)
    {
        controllerSystem.Update(world, dt);
        controllerSystem.Move(world, controller, Vec3(0.0f, 0.0f, 1.0f), false, false, dt);
    }
    controllerSystem.Update(world, dt);

    const auto* finalTransform = world.GetComponent<TransformComponent>(controller);
    const auto* finalCharacter = world.GetComponent<CharacterControllerComponent>(controller);
    ASSERT_NE(finalTransform, nullptr);
    ASSERT_NE(finalCharacter, nullptr);

    EXPECT_GT(finalTransform->position.z, 1.0f);
    EXPECT_GT(finalTransform->position.y, 2.3f);
    EXPECT_GT(finalCharacter->groundNormal.y, 0.5f);
}

TEST(CharacterControllerStaticWorldTests, WalkableRampDoesNotSlideControllerDownhill)
{
    World world;
    CharacterControllerSystem controllerSystem;
    StaticWorldGeometry staticWorld;
    staticWorld.Build(CompileRamp());
    controllerSystem.SetStaticWorldGeometry(&staticWorld);

    Entity controller = world.CreateEntity();
    auto& transform = world.AddComponent<TransformComponent>(controller);
    transform.position = Vec3(0.0f, 2.1f, 0.2f);
    transform.scale = Vec3(1.0f, 1.0f, 1.0f);

    auto& box = world.AddComponent<BoxColliderComponent>(controller);
    box.size = Vec3(1.0f, 2.0f, 1.0f);

    auto& character = world.AddComponent<CharacterControllerComponent>(controller);
    character.groundCheckDistance = 0.15f;
    character.skinWidth = 0.02f;
    character.useGravity = true;

    const float dt = 1.0f / 60.0f;
    for (int step = 0; step < 60; ++step)
    {
        controllerSystem.Update(world, dt);
        controllerSystem.Move(world, controller, Vec3::Zero(), false, false, dt);
    }
    controllerSystem.Update(world, dt);

    const auto* finalTransform = world.GetComponent<TransformComponent>(controller);
    const auto* finalCharacter = world.GetComponent<CharacterControllerComponent>(controller);
    ASSERT_NE(finalTransform, nullptr);
    ASSERT_NE(finalCharacter, nullptr);

    EXPECT_TRUE(finalCharacter->isGrounded);
    EXPECT_NEAR(finalTransform->position.z, 0.2f, 0.15f);
    EXPECT_GT(finalCharacter->groundNormal.y, 0.5f);
}

TEST(CharacterControllerStaticWorldTests, WalkableRampDoesNotBoostTravelSpeed)
{
    World world;
    CharacterControllerSystem controllerSystem;
    StaticWorldGeometry staticWorld;
    staticWorld.Build(CompileRamp());
    controllerSystem.SetStaticWorldGeometry(&staticWorld);

    Entity controller = world.CreateEntity();
    auto& transform = world.AddComponent<TransformComponent>(controller);
    const Vec3 startPosition(0.0f, 2.1f, -0.5f);
    transform.position = startPosition;
    transform.scale = Vec3(1.0f, 1.0f, 1.0f);

    auto& box = world.AddComponent<BoxColliderComponent>(controller);
    box.size = Vec3(1.0f, 2.0f, 1.0f);

    auto& character = world.AddComponent<CharacterControllerComponent>(controller);
    character.moveSpeed = 3.5f;
    character.groundCheckDistance = 0.15f;
    character.skinWidth = 0.02f;
    character.useGravity = false;

    const float dt = 1.0f / 60.0f;
    const int steps = 40;
    for (int step = 0; step < steps; ++step)
    {
        controllerSystem.Update(world, dt);
        controllerSystem.Move(world, controller, Vec3(0.0f, 0.0f, 1.0f), false, false, dt);
    }

    const auto* finalTransform = world.GetComponent<TransformComponent>(controller);
    ASSERT_NE(finalTransform, nullptr);

    const float expectedTravel = character.moveSpeed * dt * static_cast<float>(steps);
    const float actualTravel = (finalTransform->position - startPosition).Length();
    EXPECT_LE(actualTravel, expectedTravel + 0.2f);
    EXPECT_GT(actualTravel, expectedTravel - 0.4f);
}

TEST(CharacterControllerStaticWorldTests, SlidingAlongStaticWallDoesNotBoostHorizontalSpeed)
{
    World world;
    CharacterControllerSystem controllerSystem;
    StaticWorldGeometry staticWorld;
    staticWorld.Build(CompileGroundWithLongWall());
    controllerSystem.SetStaticWorldGeometry(&staticWorld);

    Entity controller = world.CreateEntity();
    auto& transform = world.AddComponent<TransformComponent>(controller);
    const Vec3 startPosition(0.0f, 1.05f, -2.0f);
    transform.position = startPosition;
    transform.scale = Vec3(1.0f, 1.0f, 1.0f);

    auto& box = world.AddComponent<BoxColliderComponent>(controller);
    box.size = Vec3(1.0f, 2.0f, 1.0f);

    auto& character = world.AddComponent<CharacterControllerComponent>(controller);
    character.moveSpeed = 3.5f;
    character.groundCheckDistance = 0.1f;
    character.skinWidth = 0.02f;
    character.useGravity = false;

    const Vec3 inputDirection = Vec3(1.0f, 0.0f, 1.0f).Normalized();
    const float dt = 1.0f / 60.0f;
    const int steps = 90;
    for (int step = 0; step < steps; ++step)
    {
        controllerSystem.Update(world, dt);
        controllerSystem.Move(world, controller, inputDirection, false, false, dt);
    }

    const auto* finalTransform = world.GetComponent<TransformComponent>(controller);
    ASSERT_NE(finalTransform, nullptr);

    const Vec3 horizontalDelta(finalTransform->position.x - startPosition.x, 0.0f, finalTransform->position.z - startPosition.z);
    const float actualHorizontalTravel = horizontalDelta.Length();
    const float expectedHorizontalTravel = character.moveSpeed * dt * static_cast<float>(steps);

    EXPECT_LE(actualHorizontalTravel, expectedHorizontalTravel + 0.1f);
    EXPECT_LT(finalTransform->position.x, 1.35f);
    EXPECT_GT(finalTransform->position.z, startPosition.z + 1.0f);
}

TEST(CharacterControllerStaticWorldTests, ControllerStepsOntoLowStaticBrushLip)
{
    World world;
    CharacterControllerSystem controllerSystem;
    StaticWorldGeometry staticWorld;
    staticWorld.Build(CompileGroundWithLowStep());
    controllerSystem.SetStaticWorldGeometry(&staticWorld);

    Entity controller = world.CreateEntity();
    auto& transform = world.AddComponent<TransformComponent>(controller);
    transform.position = Vec3(0.0f, 1.05f, -1.2f);
    transform.scale = Vec3(1.0f, 1.0f, 1.0f);

    auto& box = world.AddComponent<BoxColliderComponent>(controller);
    box.size = Vec3(1.0f, 2.0f, 1.0f);

    auto& character = world.AddComponent<CharacterControllerComponent>(controller);
    character.moveSpeed = 3.0f;
    character.groundCheckDistance = 0.1f;
    character.skinWidth = 0.02f;
    character.stepHeight = 0.3f;
    character.useGravity = false;

    const float dt = 1.0f / 60.0f;
    for (int step = 0; step < 45; ++step)
    {
        controllerSystem.Update(world, dt);
        controllerSystem.Move(world, controller, Vec3(0.0f, 0.0f, 1.0f), false, false, dt);
    }
    controllerSystem.Update(world, dt);

    const auto* finalTransform = world.GetComponent<TransformComponent>(controller);
    const auto* finalCharacter = world.GetComponent<CharacterControllerComponent>(controller);
    ASSERT_NE(finalTransform, nullptr);
    ASSERT_NE(finalCharacter, nullptr);

    EXPECT_GT(finalTransform->position.z, 0.1f);
    EXPECT_GT(finalTransform->position.y, 1.12f);
}

TEST(CharacterControllerStaticWorldTests, ControllerGetsOverRampEndLipOntoPlatform)
{
    World world;
    CharacterControllerSystem controllerSystem;
    StaticWorldGeometry staticWorld;
    staticWorld.Build(CompileRampIntoPlatform());
    controllerSystem.SetStaticWorldGeometry(&staticWorld);

    Entity controller = world.CreateEntity();
    auto& transform = world.AddComponent<TransformComponent>(controller);
    transform.position = Vec3(0.0f, 1.85f, -0.8f);
    transform.scale = Vec3(1.0f, 1.0f, 1.0f);

    auto& box = world.AddComponent<BoxColliderComponent>(controller);
    box.size = Vec3(1.0f, 2.0f, 1.0f);

    auto& character = world.AddComponent<CharacterControllerComponent>(controller);
    character.moveSpeed = 3.5f;
    character.groundCheckDistance = 0.15f;
    character.skinWidth = 0.02f;
    character.stepHeight = 0.35f;
    character.useGravity = false;

    const float dt = 1.0f / 60.0f;
    for (int step = 0; step < 70; ++step)
    {
        controllerSystem.Update(world, dt);
        controllerSystem.Move(world, controller, Vec3(0.0f, 0.0f, 1.0f), false, false, dt);
    }

    const auto* finalTransform = world.GetComponent<TransformComponent>(controller);
    ASSERT_NE(finalTransform, nullptr);

    EXPECT_GT(finalTransform->position.z, 2.4f);
    EXPECT_GT(finalTransform->position.y, 2.35f);
}

TEST(MapComponentReflectionTests, MapComponentIsRegisteredWithSerializableProperties)
{
    RegisterSceneComponents();

    const TypeInfo* info = TypeRegistry::Get().GetType("MapComponent");
    ASSERT_NE(info, nullptr);
    EXPECT_NE(info->GetProperty("mapPath"), nullptr);
    EXPECT_NE(info->GetProperty("visible"), nullptr);
    EXPECT_NE(info->GetProperty("collisionEnabled"), nullptr);
}
