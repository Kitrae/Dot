#include "../../Editor/Source/Settings/ProjectSettingsStorage.h"
#include "../../Engine/Runtime/Public/Core/Rendering/ViewSettings.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

using namespace Dot;

namespace
{

struct ViewSettingsStateGuard
{
    ViewSettingsStateGuard()
        : view(ViewSettings::Get())
        , originalMode(view.debugVisMode)
        , originalEnabled(view.ssaoEnabled)
        , originalLegacyFlag(view.ssaoDebugFullscreen)
        , originalRadius(view.ssaoRadius)
    {
    }

    ~ViewSettingsStateGuard()
    {
        view.debugVisMode = originalMode;
        view.ssaoEnabled = originalEnabled;
        view.ssaoDebugFullscreen = originalLegacyFlag;
        view.ssaoRadius = originalRadius;
        view.SyncLegacyFromDebugVis();
    }

    ViewSettings& view;
    DebugVisMode originalMode;
    bool originalEnabled;
    bool originalLegacyFlag;
    float originalRadius;
};

struct CurrentPathGuard
{
    explicit CurrentPathGuard(std::filesystem::path newPath)
        : originalPath(std::filesystem::current_path())
    {
        std::filesystem::current_path(std::move(newPath));
    }

    ~CurrentPathGuard()
    {
        std::filesystem::current_path(originalPath);
    }

    std::filesystem::path originalPath;
};

std::string ReadAllText(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

} // namespace

TEST(ViewSettingsTests, AmbientOcclusionModeStaysAvailableWithoutLegacyFullscreenFlag)
{
    ViewSettingsStateGuard guard;

    auto& view = ViewSettings::Get();
    view.debugVisMode = DebugVisMode::AmbientOcclusionOnly;
    view.ssaoEnabled = true;
    view.ssaoDebugFullscreen = false;

    view.SyncLegacyFromDebugVis();

    EXPECT_TRUE(IsDebugVisModeAvailable(DebugVisMode::AmbientOcclusionOnly));
    EXPECT_EQ(view.debugVisMode, DebugVisMode::AmbientOcclusionOnly);
    EXPECT_FALSE(view.ssaoDebugFullscreen);
    EXPECT_TRUE(view.ssaoEnabled);
}

TEST(ProjectSettingsStorageTests, LoadKeepsAmbientOcclusionModeButDropsLegacyFullscreenToggle)
{
    namespace fs = std::filesystem;

    ViewSettingsStateGuard guard;

    const fs::path tempRoot = fs::temp_directory_path() / "dot_project_settings_ao_roundtrip";
    fs::remove_all(tempRoot);
    fs::create_directories(tempRoot / "Config");

    {
        std::ofstream file(tempRoot / "Config" / "ProjectSettings.ini", std::ios::trunc);
        file << "[View]\n";
        file << "DebugVisMode=" << static_cast<int>(DebugVisMode::AmbientOcclusionOnly) << '\n';
        file << "SSAOEnabled=1\n";
        file << "SSAODebugFullscreen=1\n";
        file << "SSAORadius=0.75\n";
    }

    {
        CurrentPathGuard pathGuard(tempRoot);
        ASSERT_TRUE(ProjectSettingsStorage::Load());

        auto& view = ViewSettings::Get();
        EXPECT_EQ(view.debugVisMode, DebugVisMode::AmbientOcclusionOnly);
        EXPECT_TRUE(view.ssaoEnabled);
        EXPECT_FALSE(view.ssaoDebugFullscreen);
        EXPECT_FLOAT_EQ(view.ssaoRadius, 0.75f);
    }

}

TEST(ProjectSettingsStorageTests, SaveOmitsRetiredSsaoKeys)
{
    namespace fs = std::filesystem;

    ViewSettingsStateGuard guard;

    auto& view = ViewSettings::Get();
    view.debugVisMode = DebugVisMode::AmbientOcclusionOnly;
    view.ssaoEnabled = true;
    view.ssaoRadius = 0.85f;
    view.ssaoIntensity = 1.25f;
    view.ssaoBlurDepthThreshold = 3.5f;
    view.ssaoSampleCount = 9;
    view.ssaoHalfResolution = false;

    const fs::path tempRoot = fs::temp_directory_path() / "dot_project_settings_ao_save_contract";
    fs::remove_all(tempRoot);
    fs::create_directories(tempRoot / "Config");

    {
        CurrentPathGuard pathGuard(tempRoot);
        ASSERT_TRUE(ProjectSettingsStorage::Save());
    }

    const std::string contents = ReadAllText(tempRoot / "Config" / "ProjectSettings.ini");
    EXPECT_EQ(contents.find("SSAODebugFullscreen"), std::string::npos);
    EXPECT_EQ(contents.find("SSAOBias"), std::string::npos);
    EXPECT_EQ(contents.find("SSAOPower"), std::string::npos);
    EXPECT_EQ(contents.find("SSAOThickness"), std::string::npos);
    EXPECT_EQ(contents.find("SSAOMaxScreenRadius"), std::string::npos);
    EXPECT_EQ(contents.find("SSAOPreferExternalShaders"), std::string::npos);

    fs::remove_all(tempRoot);
}

TEST(ProjectSettingsStorageTests, LoadRestoresWireframeOverlayFromLegacyRenderMode)
{
    namespace fs = std::filesystem;

    ViewSettingsStateGuard guard;

    const fs::path tempRoot = fs::temp_directory_path() / "dot_project_settings_legacy_wireframe_overlay";
    fs::remove_all(tempRoot);
    fs::create_directories(tempRoot / "Config");

    {
        std::ofstream file(tempRoot / "Config" / "ProjectSettings.ini", std::ios::trunc);
        file << "[View]\n";
        file << "RenderMode=0\n";
        file << "WireframeOverlay=1\n";
    }

    {
        CurrentPathGuard pathGuard(tempRoot);
        ASSERT_TRUE(ProjectSettingsStorage::Load());

        auto& view = ViewSettings::Get();
        EXPECT_EQ(view.debugVisMode, DebugVisMode::WireframeOverlay);
        EXPECT_TRUE(view.wireframeOverlay);
        EXPECT_EQ(view.renderMode, RenderMode::Normal);
    }

    fs::remove_all(tempRoot);
}

TEST(ProjectSettingsStorageTests, LoadRestoresLodVisualizationFromLegacyFlags)
{
    namespace fs = std::filesystem;

    ViewSettingsStateGuard guard;

    const fs::path tempRoot = fs::temp_directory_path() / "dot_project_settings_legacy_lod_tint";
    fs::remove_all(tempRoot);
    fs::create_directories(tempRoot / "Config");

    {
        std::ofstream file(tempRoot / "Config" / "ProjectSettings.ini", std::ios::trunc);
        file << "[View]\n";
        file << "RenderMode=0\n";
        file << "LodDebugTint=1\n";
    }

    {
        CurrentPathGuard pathGuard(tempRoot);
        ASSERT_TRUE(ProjectSettingsStorage::Load());

        auto& view = ViewSettings::Get();
        EXPECT_EQ(view.debugVisMode, DebugVisMode::LODVisualization);
        EXPECT_TRUE(view.lodDebugTint);
        EXPECT_EQ(view.renderMode, RenderMode::Normal);
    }

    fs::remove_all(tempRoot);
}
