#include "Core/Scene/ComponentReflection.h"
#include "Core/Scene/LightComponent.h"
#include "Core/Scene/PointLightShadowUtils.h"
#include "Core/Reflect/Registry.h"

#include <gtest/gtest.h>

using namespace Dot;

TEST(PointLightShadowSelectionTests, SelectsNearestShadowedLightsWithStableTieBreak)
{
    const std::vector<PointLightShadowCandidate> candidates = {
        {42u, 0, Vec3(0.0f, 0.0f, 5.0f), 10.0f, true},
        {5u, 1, Vec3(0.0f, 0.0f, 2.0f), 10.0f, true},
        {17u, 2, Vec3(0.0f, 0.0f, 2.0f), 10.0f, true},
        {99u, 3, Vec3(0.0f, 0.0f, 8.0f), 10.0f, false},
        {1u, 4, Vec3(0.0f, 0.0f, 1.0f), 10.0f, true},
        {77u, 5, Vec3(0.0f, 0.0f, 3.0f), 10.0f, true},
    };

    const std::vector<SelectedPointLightShadow> selected =
        SelectPointLightsForShadows(candidates, Vec3::Zero(), kMaxShadowedPointLights);

    ASSERT_EQ(selected.size(), 4u);
    EXPECT_EQ(selected[0].lightId, 1u);
    EXPECT_EQ(selected[0].shadowBaseSlice, 0u);
    EXPECT_EQ(selected[1].lightId, 5u);
    EXPECT_EQ(selected[1].shadowBaseSlice, 6u);
    EXPECT_EQ(selected[2].lightId, 17u);
    EXPECT_EQ(selected[2].shadowBaseSlice, 12u);
    EXPECT_EQ(selected[3].lightId, 77u);
    EXPECT_EQ(selected[3].shadowBaseSlice, 18u);
}

TEST(PointLightShadowMatrixTests, BuildsSixValidCubeFacesUsingLightRangeAsFarPlane)
{
    const auto faces = BuildPointLightShadowFaces(Vec3(2.0f, 3.0f, 4.0f), 12.5f);

    EXPECT_TRUE(faces[0].forward.ApproxEqual(Vec3::UnitX()));
    EXPECT_TRUE(faces[1].forward.ApproxEqual(-Vec3::UnitX()));
    EXPECT_TRUE(faces[2].forward.ApproxEqual(Vec3::UnitY()));
    EXPECT_TRUE(faces[3].forward.ApproxEqual(-Vec3::UnitY()));
    EXPECT_TRUE(faces[4].forward.ApproxEqual(Vec3::UnitZ()));
    EXPECT_TRUE(faces[5].forward.ApproxEqual(-Vec3::UnitZ()));

    for (const PointLightShadowFace& face : faces)
    {
        EXPECT_GT(face.nearPlane, 0.0f);
        EXPECT_FLOAT_EQ(face.farPlane, 12.5f);
        EXPECT_NEAR(face.forward.Length(), 1.0f, 0.001f);
        EXPECT_NEAR(face.up.Length(), 1.0f, 0.001f);
        EXPECT_GT(std::abs(face.viewProjection.Determinant()), 0.0001f);
    }
}

TEST(PointLightComponentReflectionTests, PointLightShadowPropertiesAreSerializable)
{
    RegisterSceneComponents();

    const TypeInfo* info = TypeRegistry::Get().GetType("PointLightComponent");
    ASSERT_NE(info, nullptr);
    EXPECT_NE(info->GetProperty("castShadows"), nullptr);
    EXPECT_NE(info->GetProperty("shadowBias"), nullptr);
}

TEST(SpotLightShadowSelectionTests, SelectsNearestShadowedSpotLightsIntoDedicatedSlices)
{
    const std::vector<SpotLightShadowCandidate> candidates = {
        {42u, 0, Vec3(0.0f, 0.0f, 5.0f), true, 10.0f},
        {5u, 1, Vec3(0.0f, 0.0f, 2.0f), true, 10.0f},
        {17u, 2, Vec3(0.0f, 0.0f, 2.0f), true, 10.0f},
        {99u, 3, Vec3(0.0f, 0.0f, 8.0f), false, 10.0f},
        {1u, 4, Vec3(0.0f, 0.0f, 1.0f), true, 10.0f},
        {77u, 5, Vec3(0.0f, 0.0f, 3.0f), true, 10.0f},
    };

    const std::vector<SelectedSpotLightShadow> selected =
        SelectSpotLightsForShadows(candidates, Vec3::Zero(), kMaxShadowedSpotLights);

    ASSERT_EQ(selected.size(), 4u);
    EXPECT_EQ(selected[0].lightId, 1u);
    EXPECT_EQ(selected[0].shadowSlice, 24u);
    EXPECT_EQ(selected[1].lightId, 5u);
    EXPECT_EQ(selected[1].shadowSlice, 25u);
    EXPECT_EQ(selected[2].lightId, 17u);
    EXPECT_EQ(selected[2].shadowSlice, 26u);
    EXPECT_EQ(selected[3].lightId, 77u);
    EXPECT_EQ(selected[3].shadowSlice, 27u);
}

TEST(SpotLightShadowMatrixTests, BuildsProjectedSpotShadowUsingRangeAndConeAngle)
{
    const SpotLightShadowFace face =
        BuildSpotLightShadowFace(Vec3(2.0f, 3.0f, 4.0f), Vec3(0.0f, -1.0f, 0.0f), 18.0f, 35.0f);

    EXPECT_TRUE(face.forward.ApproxEqual(Vec3(0.0f, -1.0f, 0.0f)));
    EXPECT_GT(face.nearPlane, 0.0f);
    EXPECT_FLOAT_EQ(face.farPlane, 18.0f);
    EXPECT_GT(face.outerConeAngleRadians, 0.0f);
    EXPECT_NEAR(face.forward.Length(), 1.0f, 0.001f);
    EXPECT_NEAR(face.up.Length(), 1.0f, 0.001f);
    EXPECT_GT(std::abs(face.viewProjection.Determinant()), 0.0001f);
}

TEST(SpotLightComponentReflectionTests, SpotLightShadowPropertiesAreSerializable)
{
    RegisterSceneComponents();

    const TypeInfo* info = TypeRegistry::Get().GetType("SpotLightComponent");
    ASSERT_NE(info, nullptr);
    EXPECT_NE(info->GetProperty("castShadows"), nullptr);
    EXPECT_NE(info->GetProperty("shadowBias"), nullptr);
}
