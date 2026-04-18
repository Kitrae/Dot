// =============================================================================
// Dot Engine - Asset Manager Unit Tests
// =============================================================================

#include "Core/Assets/AssetManager.h"
#include "Core/Jobs/JobSystem.h"

#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>


using namespace Dot;

class AssetManagerTests : public ::testing::Test
{
protected:
    void SetUp() override
    {
        if (!JobSystem::Get().IsInitialized())
        {
            JobSystem::Get().Initialize(4);
        }
        AssetManager::Get().Initialize();

        // Create a dummy script for testing
        std::ofstream file("test_script.lua");
        file << "print('hello from background load')";
        file.close();
    }

    void TearDown() override
    {
        AssetManager::Get().Shutdown();

        // Ensure some time for background tasks to truly terminate if they were still closing handles
        // though Shutdown clears the cache which holds the handles.
        std::error_code ec;
        if (!std::filesystem::remove("test_script.lua", ec))
        {
            // If it fails, retry once after a short sleep to be sure
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            std::filesystem::remove("test_script.lua", ec);
        }
    }
};

TEST_F(AssetManagerTests, SingleScriptLoad)
{
    auto handle = AssetManager::Get().LoadScript("test_script.lua");

    EXPECT_TRUE(handle.IsValid());

    // Wait for it
    AssetManager::Get().Wait(handle.GetInternal());

    EXPECT_TRUE(handle.IsReady());
    EXPECT_EQ(handle->GetSource(), "print('hello from background load')");
}

TEST_F(AssetManagerTests, CacheHit)
{
    auto handle1 = AssetManager::Get().LoadScript("test_script.lua");
    auto handle2 = AssetManager::Get().LoadScript("test_script.lua");

    EXPECT_EQ(handle1.GetInternal(), handle2.GetInternal());

    // WAIT for completion to release file lock!
    AssetManager::Get().Wait(handle1.GetInternal());
}

TEST_F(AssetManagerTests, AsyncIntegrity)
{
    auto handle = AssetManager::Get().LoadScript("test_script.lua");

    // Don't wait immediately, check if it's loading
    bool wasLoading = (handle->GetState() == AssetState::Loading || handle->GetState() == AssetState::Ready);
    EXPECT_TRUE(wasLoading);

    AssetManager::Get().Wait(handle.GetInternal());
    EXPECT_TRUE(handle.IsReady());
}
