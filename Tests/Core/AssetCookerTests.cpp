#include "../../Editor/Source/Export/AssetDependencyCooker.h"

#include <gtest/gtest.h>

#include <ctime>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

using namespace Dot;

TEST(AssetDependencyCookerTests, CollectsStartupSceneDependenciesAndStagesOnlyReachableAssets)
{
    const std::filesystem::path repoRoot = std::filesystem::current_path();
    const std::filesystem::path assetsRoot = repoRoot / "Assets";

    AssetDependencyCooker cooker;
    ASSERT_TRUE(cooker.Collect(assetsRoot, "Scenes/FPSHitscanTest.dotscene")) << cooker.GetLastError();

    const auto& cookedAssets = cooker.GetCookedAssets();
    EXPECT_TRUE(cookedAssets.contains("Scenes/FPSHitscanTest.dotscene"));
    EXPECT_TRUE(cookedAssets.contains("Scripts/HitscanGunDemo.lua"));
    EXPECT_TRUE(cookedAssets.contains("Scripts/HitscanTargetDemo.lua"));
    EXPECT_FALSE(cookedAssets.contains("Scripts/DoorTriggerDemo.lua"));

    const std::filesystem::path stageRoot = repoRoot / "build_fresh" / "cook_test_assets";
    if (std::filesystem::exists(stageRoot))
        std::filesystem::remove_all(stageRoot);

    ASSERT_TRUE(cooker.StageTo(stageRoot));
    EXPECT_TRUE(std::filesystem::exists(stageRoot / "Scenes" / "FPSHitscanTest.dotscene"));
    EXPECT_TRUE(std::filesystem::exists(stageRoot / "Scripts" / "HitscanGunDemo.lua"));
    EXPECT_TRUE(std::filesystem::exists(stageRoot / "Scripts" / "HitscanTargetDemo.lua"));
    EXPECT_FALSE(std::filesystem::exists(stageRoot / "Scripts" / "DoorTriggerDemo.lua"));

    const std::filesystem::path manifestPath =
        repoRoot / "build_fresh" / ("cook_test_manifest_" + std::to_string(std::time(nullptr)) + ".json");

    ASSERT_TRUE(cooker.WriteManifest(manifestPath, "Scenes/FPSHitscanTest.dotscene"));
    ASSERT_TRUE(std::filesystem::exists(manifestPath));

    std::ifstream manifestFile(manifestPath);
    ASSERT_TRUE(manifestFile.is_open());

    std::stringstream manifestBuffer;
    manifestBuffer << manifestFile.rdbuf();
    manifestFile.close();
    const std::string manifest = manifestBuffer.str();

    EXPECT_NE(manifest.find("\"startupScene\": \"Scenes/FPSHitscanTest.dotscene\""), std::string::npos);
    EXPECT_NE(manifest.find("\"path\": \"Scripts/HitscanGunDemo.lua\""), std::string::npos);
    EXPECT_NE(manifest.find("\"path\": \"Scripts/HitscanTargetDemo.lua\""), std::string::npos);
    EXPECT_EQ(manifest.find("\"path\": \"Scripts/DoorTriggerDemo.lua\""), std::string::npos);

    std::filesystem::remove_all(stageRoot);
}

TEST(AssetDependencyCookerTests, RewritesAbsoluteMaterialTexturePathsToPackagedRelativeAssets)
{
    const std::filesystem::path repoRoot = std::filesystem::current_path();
    const std::filesystem::path tempRoot =
        repoRoot / "build_fresh" / ("cook_abs_material_test_" + std::to_string(std::time(nullptr)));
    const std::filesystem::path assetsRoot = tempRoot / "Assets";
    const std::filesystem::path externalRoot = tempRoot / "external";
    const std::filesystem::path stageRoot = tempRoot / "StagedAssets";

    std::filesystem::create_directories(assetsRoot / "Scenes");
    std::filesystem::create_directories(assetsRoot / "Materials");
    std::filesystem::create_directories(externalRoot);

    const std::filesystem::path externalTexturePath = externalRoot / "AbsoluteTexture.png";
    {
        std::ofstream textureFile(externalTexturePath, std::ios::binary);
        ASSERT_TRUE(textureFile.is_open());
        textureFile << "fakepng";
    }

    const std::filesystem::path materialPath = assetsRoot / "Materials" / "AbsoluteTexture.dotmat";
    {
        std::ofstream materialFile(materialPath);
        ASSERT_TRUE(materialFile.is_open());
        materialFile << "DOTMATERIAL 1.0\n";
        materialFile << "NODES 2\n";
        materialFile << "NODE 0 2 0 0\n";
        materialFile << "PROPERTIES 0\n";
        materialFile << "INPUTS 5 6 7 8 9 10\n";
        materialFile << "OUTPUTS 0\n";
        materialFile << "NODE 4 3 -300 0\n";
        materialFile << "PROPERTIES 7\n";
        materialFile << "PROP Texture " << externalTexturePath.string() << "\n";
        materialFile << "PROP Filter_Mode 1\n";
        materialFile << "PROP Wrap_Mode 0\n";
        materialFile << "PROP Tiling 1 1\n";
        materialFile << "PROP Offset 0 0\n";
        materialFile << "PROP Texture_Slot 0\n";
        materialFile << "PROP Mipmaps 1\n";
        materialFile << "INPUTS 1 11\n";
        materialFile << "OUTPUTS 3 12 13 14\n";
        materialFile << "CONNECTIONS 1\n";
        materialFile << "CONN 1 13 6\n";
        materialFile << "END\n";
    }

    const std::filesystem::path scenePath = assetsRoot / "Scenes" / "AbsoluteTextureScene.dotscene";
    {
        std::ofstream sceneFile(scenePath);
        ASSERT_TRUE(sceneFile.is_open());
        sceneFile << "{\n";
        sceneFile << "  \"scene\": {\n";
        sceneFile << "    \"version\": 2,\n";
        sceneFile << "    \"entities\": [\n";
        sceneFile << "      {\n";
        sceneFile << "        \"id\": 1,\n";
        sceneFile << "        \"transform\": {\n";
        sceneFile << "          \"position\": [0.0, 0.0, 0.0],\n";
        sceneFile << "          \"rotation\": [0.0, 0.0, 0.0],\n";
        sceneFile << "          \"scale\": [1.0, 1.0, 1.0]\n";
        sceneFile << "        },\n";
        sceneFile << "        \"name\": {\n";
        sceneFile << "          \"name\": \"TexturedEntity\"\n";
        sceneFile << "        },\n";
        sceneFile << "        \"primitive\": {\n";
        sceneFile << "          \"type\": 0\n";
        sceneFile << "        },\n";
        sceneFile << "        \"material\": {\n";
        sceneFile << "          \"materialPath\": \"Materials/AbsoluteTexture.dotmat\",\n";
        sceneFile << "          \"useMaterialFile\": true,\n";
        sceneFile << "          \"baseColor\": [1.0, 1.0, 1.0],\n";
        sceneFile << "          \"metallic\": 0.0,\n";
        sceneFile << "          \"roughness\": 1.0\n";
        sceneFile << "        }\n";
        sceneFile << "      }\n";
        sceneFile << "    ]\n";
        sceneFile << "  }\n";
        sceneFile << "}\n";
    }

    AssetDependencyCooker cooker;
    ASSERT_TRUE(cooker.Collect(assetsRoot, "Scenes/AbsoluteTextureScene.dotscene")) << cooker.GetLastError();
    ASSERT_TRUE(cooker.GetCookedAssets().contains("Textures/AbsoluteTexture.png"));

    ASSERT_TRUE(cooker.StageTo(stageRoot));
    ASSERT_TRUE(std::filesystem::exists(stageRoot / "Textures" / "AbsoluteTexture.png"));
    ASSERT_TRUE(std::filesystem::exists(stageRoot / "Materials" / "AbsoluteTexture.dotmat"));

    std::ifstream stagedMaterial(stageRoot / "Materials" / "AbsoluteTexture.dotmat");
    ASSERT_TRUE(stagedMaterial.is_open());
    std::stringstream stagedBuffer;
    stagedBuffer << stagedMaterial.rdbuf();
    stagedMaterial.close();
    const std::string stagedText = stagedBuffer.str();

    EXPECT_NE(stagedText.find("PROP Texture Textures/AbsoluteTexture.png"), std::string::npos);
    EXPECT_EQ(stagedText.find(externalTexturePath.string()), std::string::npos);

}
