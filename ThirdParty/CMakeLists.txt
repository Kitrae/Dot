# =============================================================================
# Dot Engine - Third Party Dependencies
# =============================================================================

# This file manages external dependencies using CMake's FetchContent
include(FetchContent)

# -----------------------------------------------------------------------------
# GoogleTest (for unit testing)
# -----------------------------------------------------------------------------
if(DOT_BUILD_TESTS)
    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG        v1.14.0
        GIT_SHALLOW    TRUE
    )
    # Prevent GoogleTest from overriding our compiler/linker options
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
    set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
    
    FetchContent_MakeAvailable(googletest)
endif()

# -----------------------------------------------------------------------------
# Dear ImGui (for Editor UI)
# -----------------------------------------------------------------------------
if(DOT_BUILD_EDITOR)
    FetchContent_Declare(
        imgui
        GIT_REPOSITORY https://github.com/ocornut/imgui.git
        GIT_TAG        docking
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(imgui)

    # Create ImGui library with backends we need
    add_library(imgui STATIC
        ${imgui_SOURCE_DIR}/imgui.cpp
        ${imgui_SOURCE_DIR}/imgui_demo.cpp
        ${imgui_SOURCE_DIR}/imgui_draw.cpp
        ${imgui_SOURCE_DIR}/imgui_tables.cpp
        ${imgui_SOURCE_DIR}/imgui_widgets.cpp
        # Win32 + D3D12 backends
        ${imgui_SOURCE_DIR}/backends/imgui_impl_win32.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_dx12.cpp
    )

    target_include_directories(imgui PUBLIC
        ${imgui_SOURCE_DIR}
        ${imgui_SOURCE_DIR}/backends
    )

    # Link D3D12 libs for the backend
    if(WIN32)
        target_link_libraries(imgui PRIVATE d3d12 dxgi)
        # Disable warnings as errors for third-party code
        target_compile_options(imgui PRIVATE /WX-)
    endif()

    set_target_properties(imgui PROPERTIES FOLDER "ThirdParty")

    # -------------------------------------------------------------------------
    # ImNodes (for Material Graph Editor) - TEMPORARILY DISABLED
    # ImNodes requires specific ImGui version, causing compatibility issues
    # with ImGui docking branch. Will be re-enabled when compatibility is fixed.
    # -------------------------------------------------------------------------
    # FetchContent_Declare(
    #     imnodes
    #     GIT_REPOSITORY https://github.com/Nelarius/imnodes.git
    #     GIT_TAG        master
    #     GIT_SHALLOW    TRUE
    # )
    # FetchContent_MakeAvailable(imnodes)
    # add_library(imnodes STATIC ${imnodes_SOURCE_DIR}/imnodes.cpp)
    # target_include_directories(imnodes PUBLIC ${imnodes_SOURCE_DIR})
    # target_link_libraries(imnodes PUBLIC imgui)
    # target_compile_definitions(imnodes PUBLIC IMGUI_DEFINE_MATH_OPERATORS)
    # set_target_properties(imnodes PROPERTIES FOLDER "ThirdParty")
endif()

# -----------------------------------------------------------------------------
# LuaJIT - Fast Lua JIT Compiler
# -----------------------------------------------------------------------------
# Use ExternalProject to build LuaJIT with its native build system
include(ExternalProject)

# Download and build LuaJIT using its native MSVC build script
ExternalProject_Add(luajit_external
    GIT_REPOSITORY    https://github.com/LuaJIT/LuaJIT.git
    GIT_TAG           v2.1
    GIT_SHALLOW       TRUE
    UPDATE_DISCONNECTED TRUE  # Don't check for updates on every build
    PREFIX            ${CMAKE_BINARY_DIR}/_deps/luajit
    CONFIGURE_COMMAND ""
    BUILD_COMMAND     cmd /c "cd src && msvcbuild.bat static && if not exist luajit.h (if exist luajit_rolling.h copy /Y luajit_rolling.h luajit.h)"
    BUILD_IN_SOURCE   TRUE
    INSTALL_COMMAND   ""
    BUILD_BYPRODUCTS  <SOURCE_DIR>/src/lua51.lib
)

# Get the source directory
ExternalProject_Get_Property(luajit_external SOURCE_DIR)
set(LUAJIT_SRC_DIR ${SOURCE_DIR}/src)

# Create imported library target
add_library(libluajit STATIC IMPORTED GLOBAL)
set_target_properties(libluajit PROPERTIES
    IMPORTED_LOCATION ${LUAJIT_SRC_DIR}/lua51.lib
    INTERFACE_INCLUDE_DIRECTORIES ${LUAJIT_SRC_DIR}
)
add_dependencies(libluajit luajit_external)

# sol2 - Modern C++ Lua bindings (header-only)
FetchContent_Declare(
    sol2
    GIT_REPOSITORY https://github.com/ThePhD/sol2.git
    GIT_TAG        v3.3.1
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(sol2)

# Create interface target for sol2 with LuaJIT
add_library(sol2_interface INTERFACE)
target_include_directories(sol2_interface INTERFACE ${sol2_SOURCE_DIR}/include)
target_link_libraries(sol2_interface INTERFACE libluajit)
# Tell sol2 we're using LuaJIT
target_compile_definitions(sol2_interface INTERFACE SOL_LUAJIT=1)
set_target_properties(sol2_interface PROPERTIES FOLDER "ThirdParty")

# Recast Navigation - navmesh generation and pathfinding
FetchContent_Declare(
    recastnavigation
    GIT_REPOSITORY https://github.com/recastnavigation/recastnavigation.git
    GIT_TAG        main
    GIT_SHALLOW    TRUE
)
set(RECASTNAVIGATION_DEMO OFF CACHE BOOL "" FORCE)
set(RECASTNAVIGATION_EXAMPLES OFF CACHE BOOL "" FORCE)
set(RECASTNAVIGATION_TESTS OFF CACHE BOOL "" FORCE)
set(RECASTNAVIGATION_STATIC ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(recastnavigation)
set_target_properties(Recast PROPERTIES FOLDER "ThirdParty")
set_target_properties(Detour PROPERTIES FOLDER "ThirdParty")

# -----------------------------------------------------------------------------
# Future Dependencies (uncomment as needed)
# -----------------------------------------------------------------------------

# # GLFW - Windowing
# FetchContent_Declare(
#     glfw
#     GIT_REPOSITORY https://github.com/glfw/glfw.git
#     GIT_TAG        3.3.9
#     GIT_SHALLOW    TRUE
# )
# set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
# set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
# set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
# FetchContent_MakeAvailable(glfw)

# # GLM - Math library (temporary until SIMD math is complete)
# FetchContent_Declare(
#     glm
#     GIT_REPOSITORY https://github.com/g-truc/glm.git
#     GIT_TAG        1.0.1
#     GIT_SHALLOW    TRUE
# )
# FetchContent_MakeAvailable(glm)

# # spdlog - Logging
# FetchContent_Declare(
#     spdlog
#     GIT_REPOSITORY https://github.com/gabime/spdlog.git
#     GIT_TAG        v1.12.0
#     GIT_SHALLOW    TRUE
# )
# FetchContent_MakeAvailable(spdlog)

# # Jolt Physics (will be added in Stage 5)


# -----------------------------------------------------------------------------
# ufbx - Single-source FBX Loader
# -----------------------------------------------------------------------------
add_library(ufbx STATIC
    ufbx/ufbx.c
    ufbx/ufbx.h
)
target_include_directories(ufbx PUBLIC ufbx)
if(MSVC)
    target_compile_options(ufbx PRIVATE /WX-)
endif()
set_target_properties(ufbx PROPERTIES FOLDER "ThirdParty")
