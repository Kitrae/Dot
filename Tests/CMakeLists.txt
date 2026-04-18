# =============================================================================
# Dot Engine - Tests
# =============================================================================

file(GLOB_RECURSE DOT_TEST_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/Core/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/RHI/*.cpp"
)

set(DOT_TEST_EDITOR_SUPPORT_SOURCES
    "${CMAKE_SOURCE_DIR}/Editor/Source/Commands/CommandRegistry.cpp"
    "${CMAKE_SOURCE_DIR}/Editor/Source/Commands/EntityClipboard.cpp"
    "${CMAKE_SOURCE_DIR}/Editor/Source/Export/AssetDependencyCooker.cpp"
    "${CMAKE_SOURCE_DIR}/Editor/Source/Map/MapDocument.cpp"
    "${CMAKE_SOURCE_DIR}/Editor/Source/Scene/EditorSelectionState.cpp"
    "${CMAKE_SOURCE_DIR}/Editor/Source/Scene/SceneSerializer.cpp"
    "${CMAKE_SOURCE_DIR}/Editor/Source/Scene/SceneSettingsSerializer.cpp"
    "${CMAKE_SOURCE_DIR}/Editor/Source/Scripting/LuaAutoComplete.cpp"
    "${CMAKE_SOURCE_DIR}/Editor/Source/Settings/ProjectSettingsStorage.cpp"
    "${CMAKE_SOURCE_DIR}/Editor/Source/Toolbox/ToolboxManager.cpp"
)

add_executable(DotTests
    ${DOT_TEST_SOURCES}
    ${DOT_TEST_EDITOR_SUPPORT_SOURCES}
)

target_include_directories(DotTests
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/Editor/Source
        ${CMAKE_SOURCE_DIR}/Engine/RHI/Private
)

target_link_libraries(DotTests
    PRIVATE
        Dot::Core
        Dot::RHI
        Dot::Runtime
        imgui
        ufbx
        gtest
        gtest_main
        Recast
        Detour
)

if(WIN32)
    target_link_libraries(DotTests PRIVATE d3d12 dxgi d3dcompiler)
endif()

if(MSVC)
    target_compile_options(DotTests PRIVATE /bigobj /Zm300)
endif()

include(GoogleTest)
gtest_discover_tests(DotTests)

set_target_properties(DotTests PROPERTIES
    FOLDER "Tests"
)
