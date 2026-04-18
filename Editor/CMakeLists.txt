# =============================================================================
# Dot Engine - Editor Application
# =============================================================================

file(GLOB_RECURSE EDITOR_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/*.cpp"
)

list(FILTER EDITOR_SOURCES EXCLUDE REGEX ".*/Source/Scene/SceneSerializer\\.cpp$")
list(FILTER EDITOR_SOURCES EXCLUDE REGEX ".*/Source/Scene/SceneSettingsSerializer\\.cpp$")
list(FILTER EDITOR_SOURCES EXCLUDE REGEX ".*/Source/Rendering/CubemapLoader\\.cpp$")
list(FILTER EDITOR_SOURCES EXCLUDE REGEX ".*/Source/Rendering/ShaderCompiler\\.cpp$")
list(FILTER EDITOR_SOURCES EXCLUDE REGEX ".*/Source/Rendering/SimpleRenderer\\.cpp$")
list(FILTER EDITOR_SOURCES EXCLUDE REGEX ".*/Source/Rendering/SimpleRenderer\\.CullingLighting\\.cpp$")
list(FILTER EDITOR_SOURCES EXCLUDE REGEX ".*/Source/Rendering/SimpleRenderer\\.PostProcess\\.cpp$")
list(FILTER EDITOR_SOURCES EXCLUDE REGEX ".*/Source/Rendering/SimpleRendererGraphPasses\\.cpp$")

set(EDITOR_SHARED_RENDERING_SOURCES
    "${CMAKE_SOURCE_DIR}/Engine/Runtime/Shared/Rendering/CubemapLoader.cpp"
    "${CMAKE_SOURCE_DIR}/Engine/Runtime/Shared/Rendering/ShaderCompiler.cpp"
    "${CMAKE_SOURCE_DIR}/Engine/Runtime/Shared/Rendering/SimpleRenderer.cpp"
    "${CMAKE_SOURCE_DIR}/Engine/Runtime/Shared/Rendering/SimpleRenderer.CullingLighting.cpp"
    "${CMAKE_SOURCE_DIR}/Engine/Runtime/Shared/Rendering/SimpleRenderer.PostProcess.cpp"
    "${CMAKE_SOURCE_DIR}/Engine/Runtime/Shared/Rendering/SimpleRendererGraphPasses.cpp"
)

set(EDITOR_THIRD_PARTY_SOURCES
    "${CMAKE_SOURCE_DIR}/ThirdParty/ImGuiColorTextEdit/TextEditor.cpp"
)

add_executable(DotEditor WIN32
    ${EDITOR_SOURCES}
    ${EDITOR_SHARED_RENDERING_SOURCES}
    ${EDITOR_THIRD_PARTY_SOURCES}
)

target_include_directories(DotEditor
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/Source
        ${CMAKE_SOURCE_DIR}/Engine/Runtime/Shared/Rendering
        ${CMAKE_SOURCE_DIR}/Engine/RHI/Private
        ${CMAKE_SOURCE_DIR}/ThirdParty/ImGuiColorTextEdit
)

target_link_libraries(DotEditor
    PRIVATE
        Dot::Core
        Dot::RHI
        Dot::Runtime
        imgui
        ufbx
        Recast
        Detour
)

if(WIN32)
    target_link_libraries(DotEditor PRIVATE d3d12 dxgi d3dcompiler windowscodecs ole32)
endif()

if(MSVC)
    target_compile_options(DotEditor PRIVATE /bigobj /Zm300)
endif()

set_target_properties(DotEditor PROPERTIES
    FOLDER "Editor"
)

if(TARGET DotCrashReporter)
    add_dependencies(DotEditor DotCrashReporter)
endif()
