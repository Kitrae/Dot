# =============================================================================
# Dot Engine - Runtime Module
# =============================================================================
# Runtime gameplay stack: scene, physics, input, scripting, runtime interfaces.
# =============================================================================

file(GLOB_RECURSE RUNTIME_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/Private/*.cpp"
)

file(GLOB_RECURSE RUNTIME_HEADERS CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/Public/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/Private/*.h"
)

add_library(DotRuntime STATIC
    ${RUNTIME_SOURCES}
    ${RUNTIME_HEADERS}
)

add_library(Dot::Runtime ALIAS DotRuntime)

target_include_directories(DotRuntime
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/Public
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/Private
)

target_link_libraries(DotRuntime
    PUBLIC
        Dot::Core
        imgui
        Recast
        Detour
)

# Runtime currently includes script/physics codepaths that may pull third-party
# headers with noisy warnings on some MSVC versions.
if(MSVC)
    target_compile_options(DotRuntime PRIVATE /W3 /WX- /bigobj)
endif()

set_target_properties(DotRuntime PROPERTIES
    FOLDER "Engine"
)

source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/Public" PREFIX "Public" FILES
    ${RUNTIME_HEADERS}
)
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/Private" PREFIX "Private" FILES
    ${RUNTIME_SOURCES}
)
