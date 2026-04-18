// =============================================================================
// Dot Engine - Toolbox Tests
// =============================================================================

#include "../../Editor/Source/Toolbox/ToolboxManager.h"
#include "../../Editor/Source/Toolbox/ToolboxSettings.h"
#include "../../Editor/Source/Scripting/LuaAutoComplete.h"
#include "Core/Toolbox/ModuleIds.h"

#include <gtest/gtest.h>

using namespace Dot;

class ToolboxManagerTests : public ::testing::Test
{
protected:
    void SetUp() override
    {
        ToolboxManager::Get().Shutdown();
        ToolboxSettings::Get().moduleStates.clear();
        ToolboxSettings::Get().showAdvancedModules = false;
        ToolboxManager::Get().Initialize();
    }

    void TearDown() override
    {
        ToolboxManager::Get().Shutdown();
        ToolboxSettings::Get().moduleStates.clear();
        ToolboxSettings::Get().showAdvancedModules = false;
    }
};

TEST_F(ToolboxManagerTests, DefaultsEnableCurrentEditorAndRuntimeFeatures)
{
    EXPECT_TRUE(ToolboxManager::Get().IsPhysicsEnabled());
    EXPECT_TRUE(ToolboxManager::Get().IsNavigationEnabled());
    EXPECT_TRUE(ToolboxManager::Get().IsPlayerInputEnabled());
    EXPECT_TRUE(ToolboxManager::Get().IsScriptingEnabled());
    EXPECT_TRUE(ToolboxManager::Get().IsMapEditorEnabled());
    EXPECT_TRUE(ToolboxManager::Get().IsMaterialEditorEnabled());
}

TEST_F(ToolboxManagerTests, DisablingPhysicsDisablesDependentModules)
{
    ToolboxManager::Get().SetDraftModuleEnabled(ToolboxModuleIds::kPhysics, false);
    ToolboxManager::Get().ApplyDraft();

    EXPECT_FALSE(ToolboxManager::Get().IsPhysicsEnabled());
    EXPECT_FALSE(ToolboxManager::Get().IsNavigationEnabled());
    EXPECT_FALSE(ToolboxManager::Get().IsPlayerInputEnabled());

    const SceneRuntimeModuleConfig config = ToolboxManager::Get().BuildSceneRuntimeConfig();
    EXPECT_FALSE(config.enablePhysics);
    EXPECT_FALSE(config.enableNavigation);
    EXPECT_FALSE(config.enablePlayerInput);
    EXPECT_FALSE(config.scriptFeatures.enablePhysicsBindings);
    EXPECT_FALSE(config.scriptFeatures.enableNavigationBindings);
    EXPECT_FALSE(config.scriptFeatures.enablePerceptionBindings);
}

TEST_F(ToolboxManagerTests, EnablingNavigationAlsoEnablesPhysics)
{
    ToolboxManager::Get().SetDraftModuleEnabled(ToolboxModuleIds::kPhysics, false);
    ToolboxManager::Get().ApplyDraft();

    ToolboxManager::Get().ResetDraft();
    ToolboxManager::Get().SetDraftModuleEnabled(ToolboxModuleIds::kNavigation, true);
    ToolboxManager::Get().ApplyDraft();

    EXPECT_TRUE(ToolboxManager::Get().IsPhysicsEnabled());
    EXPECT_TRUE(ToolboxManager::Get().IsNavigationEnabled());
}

TEST_F(ToolboxManagerTests, WorkspacesFollowModuleState)
{
    EXPECT_TRUE(ToolboxManager::Get().IsWorkspaceEnabled(WorkspaceType::Layout));
    EXPECT_TRUE(ToolboxManager::Get().IsWorkspaceEnabled(WorkspaceType::Map));
    EXPECT_TRUE(ToolboxManager::Get().IsWorkspaceEnabled(WorkspaceType::Material));
    EXPECT_TRUE(ToolboxManager::Get().IsWorkspaceEnabled(WorkspaceType::Scripting));

    ToolboxManager::Get().SetDraftModuleEnabled(ToolboxModuleIds::kMapEditor, false);
    ToolboxManager::Get().SetDraftModuleEnabled(ToolboxModuleIds::kMaterialEditor, false);
    ToolboxManager::Get().SetDraftModuleEnabled(ToolboxModuleIds::kScripting, false);
    ToolboxManager::Get().ApplyDraft();

    EXPECT_FALSE(ToolboxManager::Get().IsWorkspaceEnabled(WorkspaceType::Map));
    EXPECT_FALSE(ToolboxManager::Get().IsWorkspaceEnabled(WorkspaceType::Material));
    EXPECT_FALSE(ToolboxManager::Get().IsWorkspaceEnabled(WorkspaceType::Scripting));
}

TEST_F(ToolboxManagerTests, LuaAutocompleteHidesPhysicsBindingsWhenDisabled)
{
    ToolboxManager::Get().SetDraftModuleEnabled(ToolboxModuleIds::kScriptPhysicsBindings, false);
    ToolboxManager::Get().ApplyDraft();

    const auto& entries = GetLuaAutoCompleteEntries();
    const auto physicsEntry =
        std::find_if(entries.begin(), entries.end(), [](const LuaAutoCompleteEntry& entry) { return entry.keyword == "Physics.Raycast"; });
    ASSERT_NE(physicsEntry, entries.end());
    EXPECT_FALSE(IsLuaAutoCompleteEntryVisible(*physicsEntry));

    const auto worldEntry =
        std::find_if(entries.begin(), entries.end(), [](const LuaAutoCompleteEntry& entry) { return entry.keyword == "World.Spawn"; });
    ASSERT_NE(worldEntry, entries.end());
    EXPECT_TRUE(IsLuaAutoCompleteEntryVisible(*worldEntry));
}

TEST_F(ToolboxManagerTests, LuaAutocompleteHidesEngineApiWhenScriptingDisabled)
{
    ToolboxManager::Get().SetDraftModuleEnabled(ToolboxModuleIds::kScripting, false);
    ToolboxManager::Get().ApplyDraft();

    const auto& entries = GetLuaAutoCompleteEntries();
    const auto worldEntry =
        std::find_if(entries.begin(), entries.end(), [](const LuaAutoCompleteEntry& entry) { return entry.keyword == "World.Spawn"; });
    ASSERT_NE(worldEntry, entries.end());
    EXPECT_FALSE(IsLuaAutoCompleteEntryVisible(*worldEntry));

    const auto luaKeyword =
        std::find_if(entries.begin(), entries.end(), [](const LuaAutoCompleteEntry& entry) { return entry.keyword == "local"; });
    ASSERT_NE(luaKeyword, entries.end());
    EXPECT_TRUE(IsLuaAutoCompleteEntryVisible(*luaKeyword));
}

TEST_F(ToolboxManagerTests, LuaAutocompleteIncludesViewmodelCameraApi)
{
    const auto& entries = GetLuaAutoCompleteEntries();

    const auto setFovEntry =
        std::find_if(entries.begin(), entries.end(), [](const LuaAutoCompleteEntry& entry)
                     { return entry.keyword == "Camera.SetViewmodelFov"; });
    ASSERT_NE(setFovEntry, entries.end());
    EXPECT_TRUE(IsLuaAutoCompleteEntryVisible(*setFovEntry));

    const auto nearPlaneEntry =
        std::find_if(entries.begin(), entries.end(), [](const LuaAutoCompleteEntry& entry)
                     { return entry.keyword == "Camera.SetViewmodelNearPlane"; });
    ASSERT_NE(nearPlaneEntry, entries.end());
    EXPECT_TRUE(IsLuaAutoCompleteEntryVisible(*nearPlaneEntry));

    const auto visibilityEntry =
        std::find_if(entries.begin(), entries.end(), [](const LuaAutoCompleteEntry& entry)
                     { return entry.keyword == "Camera.SetViewmodelLayerVisible"; });
    ASSERT_NE(visibilityEntry, entries.end());
    EXPECT_TRUE(IsLuaAutoCompleteEntryVisible(*visibilityEntry));
}

TEST_F(ToolboxManagerTests, LuaAutocompleteIncludesEntityRenderLayerApi)
{
    const auto& entries = GetLuaAutoCompleteEntries();

    const auto maskEntry =
        std::find_if(entries.begin(), entries.end(), [](const LuaAutoCompleteEntry& entry)
                     { return entry.keyword == "self.RenderLayerMask"; });
    ASSERT_NE(maskEntry, entries.end());
    EXPECT_TRUE(IsLuaAutoCompleteEntryVisible(*maskEntry));

    const auto worldEntry =
        std::find_if(entries.begin(), entries.end(), [](const LuaAutoCompleteEntry& entry)
                     { return entry.keyword == "self.InWorldLayer"; });
    ASSERT_NE(worldEntry, entries.end());
    EXPECT_TRUE(IsLuaAutoCompleteEntryVisible(*worldEntry));

    const auto viewmodelEntry =
        std::find_if(entries.begin(), entries.end(), [](const LuaAutoCompleteEntry& entry)
                     { return entry.keyword == "self.InViewmodelLayer"; });
    ASSERT_NE(viewmodelEntry, entries.end());
    EXPECT_TRUE(IsLuaAutoCompleteEntryVisible(*viewmodelEntry));
}

TEST_F(ToolboxManagerTests, LuaAutocompleteIncludesEntityAttachmentApi)
{
    const auto& entries = GetLuaAutoCompleteEntries();

    const auto attachToEntityEntry =
        std::find_if(entries.begin(), entries.end(), [](const LuaAutoCompleteEntry& entry)
                     { return entry.keyword == "self:AttachToEntity"; });
    ASSERT_NE(attachToEntityEntry, entries.end());
    EXPECT_TRUE(IsLuaAutoCompleteEntryVisible(*attachToEntityEntry));

    const auto attachToCameraEntry =
        std::find_if(entries.begin(), entries.end(), [](const LuaAutoCompleteEntry& entry)
                     { return entry.keyword == "self:AttachToCamera"; });
    ASSERT_NE(attachToCameraEntry, entries.end());
    EXPECT_TRUE(IsLuaAutoCompleteEntryVisible(*attachToCameraEntry));

    const auto socketEntry =
        std::find_if(entries.begin(), entries.end(), [](const LuaAutoCompleteEntry& entry)
                     { return entry.keyword == "self.AttachmentSocket"; });
    ASSERT_NE(socketEntry, entries.end());
    EXPECT_TRUE(IsLuaAutoCompleteEntryVisible(*socketEntry));
}

TEST_F(ToolboxManagerTests, LuaAutocompleteIncludesSocketLookupHelpers)
{
    const auto& entries = GetLuaAutoCompleteEntries();

    const auto entityCreateEntry =
        std::find_if(entries.begin(), entries.end(), [](const LuaAutoCompleteEntry& entry)
                     { return entry.keyword == "self:CreateSocket"; });
    ASSERT_NE(entityCreateEntry, entries.end());
    EXPECT_TRUE(IsLuaAutoCompleteEntryVisible(*entityCreateEntry));

    const auto entityGetEntry =
        std::find_if(entries.begin(), entries.end(), [](const LuaAutoCompleteEntry& entry)
                     { return entry.keyword == "self:GetSocket"; });
    ASSERT_NE(entityGetEntry, entries.end());
    EXPECT_TRUE(IsLuaAutoCompleteEntryVisible(*entityGetEntry));

    const auto cameraCreateEntry =
        std::find_if(entries.begin(), entries.end(), [](const LuaAutoCompleteEntry& entry)
                     { return entry.keyword == "Camera.CreateSocket"; });
    ASSERT_NE(cameraCreateEntry, entries.end());
    EXPECT_TRUE(IsLuaAutoCompleteEntryVisible(*cameraCreateEntry));

    const auto cameraGetEntry =
        std::find_if(entries.begin(), entries.end(), [](const LuaAutoCompleteEntry& entry)
                     { return entry.keyword == "Camera.GetSocket"; });
    ASSERT_NE(cameraGetEntry, entries.end());
    EXPECT_TRUE(IsLuaAutoCompleteEntryVisible(*cameraGetEntry));
}
