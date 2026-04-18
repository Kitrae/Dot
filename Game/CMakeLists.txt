# =============================================================================
# Dot Engine - Standalone Game
# =============================================================================

set(GAME_SHARED_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/RuntimeGameApp.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/RuntimeSceneRenderer.cpp
    ${CMAKE_SOURCE_DIR}/Engine/Runtime/Shared/Rendering/CubemapLoader.cpp
    ${CMAKE_SOURCE_DIR}/Engine/Runtime/Shared/Rendering/ShaderCompiler.cpp
    ${CMAKE_SOURCE_DIR}/Engine/Runtime/Shared/Rendering/SimpleRenderer.cpp
    ${CMAKE_SOURCE_DIR}/Engine/Runtime/Shared/Rendering/SimpleRenderer.CullingLighting.cpp
    ${CMAKE_SOURCE_DIR}/Engine/Runtime/Shared/Rendering/SimpleRenderer.PostProcess.cpp
    ${CMAKE_SOURCE_DIR}/Engine/Runtime/Shared/Rendering/SimpleRendererGraphPasses.cpp
)

add_library(DotGameFrontend STATIC
    ${GAME_SHARED_SOURCES}
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/RuntimeGameApp.h
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/RuntimeSceneRenderer.h
)

target_include_directories(DotGameFrontend
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/Source
    PRIVATE
        ${CMAKE_SOURCE_DIR}/Engine/Runtime/Shared/Rendering
        ${CMAKE_SOURCE_DIR}/Engine/RHI/Private
)

target_link_libraries(DotGameFrontend
    PUBLIC
        Dot::Core
        Dot::RHI
        Dot::Runtime
        ufbx
        Recast
        Detour
)

if(WIN32)
    target_link_libraries(DotGameFrontend PUBLIC d3d12 dxgi d3dcompiler shell32)
endif()

if(MSVC)
    target_compile_options(DotGameFrontend PRIVATE /bigobj /Zm300)
endif()

add_executable(DotGame WIN32
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/GameMain.cpp
)

target_link_libraries(DotGame PRIVATE DotGameFrontend)

if(MSVC)
    target_compile_options(DotGame PRIVATE /bigobj /Zm300)
endif()

set_target_properties(DotGameFrontend PROPERTIES
    FOLDER "Game"
)

set_target_properties(DotGame PROPERTIES
    FOLDER "Game"
)

if(TARGET DotCrashReporter)
    add_dependencies(DotGame DotCrashReporter)
endif()
