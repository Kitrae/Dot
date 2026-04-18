# =============================================================================
# Dot Engine - Core Module
# =============================================================================
# The foundational "kernel" of the engine
# Contains: Memory Management, Job System, SIMD Math
# =============================================================================

# Collect source files
file(GLOB_RECURSE CORE_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/Private/*.cpp"
)

file(GLOB_RECURSE CORE_HEADERS
    "${CMAKE_CURRENT_SOURCE_DIR}/Public/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/Private/*.h"
)

# Create static library
add_library(DotCore STATIC
    ${CORE_SOURCES}
    ${CORE_HEADERS}
)

# Alias for cleaner target references
add_library(Dot::Core ALIAS DotCore)

# Include directories
target_include_directories(DotCore
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/Public
        ${CMAKE_SOURCE_DIR}/ThirdParty
        ${CMAKE_SOURCE_DIR}/Engine/RHI/Public
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/Private
)

# Compile definitions
target_compile_definitions(DotCore
    PRIVATE
        DOT_CORE_EXPORTS  # For future DLL support
)

# Precompiled header (will be added when we have more files)
# target_precompile_headers(DotCore
#     PRIVATE
#         "${CMAKE_CURRENT_SOURCE_DIR}/Private/CorePCH.h"
# )

# Link libraries
target_link_libraries(DotCore
    PUBLIC
        sol2_interface  # Lua scripting via sol2
)

if(WIN32)
    target_link_libraries(DotCore PUBLIC dbghelp)
endif()

# Disable warnings-as-errors for DotCore because it includes third-party sol2 headers
# which may produce warnings on different compiler versions
if(MSVC)
    target_compile_options(DotCore PRIVATE /W3 /WX-)
endif()

# IDE folder organization
set_target_properties(DotCore PROPERTIES
    FOLDER "Engine"
)

# Source groups for IDE (Visual Studio, Xcode)
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/Public" PREFIX "Public" FILES 
    ${CORE_HEADERS}
)
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/Private" PREFIX "Private" FILES 
    ${CORE_SOURCES}
)
