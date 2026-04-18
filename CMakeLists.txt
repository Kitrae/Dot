# =============================================================================
# Dot Engine - Root CMake Configuration
# =============================================================================
# Professional game engine built with modern C++20
# Architecture: Data-Oriented Hybrid (ECS backend, Node frontend)
# =============================================================================

cmake_minimum_required(VERSION 3.21)

# -----------------------------------------------------------------------------
# Project Definition
# -----------------------------------------------------------------------------
project(DotEngine
    VERSION 0.1.0
    DESCRIPTION "Professional game engine with Godot ease and Unreal power"
    LANGUAGES CXX
)

# Prevent in-source builds
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)
    message(FATAL_ERROR "In-source builds are not allowed. Please use a build directory.")
endif()

# -----------------------------------------------------------------------------
# Global C++ Configuration
# -----------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Export compile commands for IDE support
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# -----------------------------------------------------------------------------
# Output Directories
# -----------------------------------------------------------------------------
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# Per-config output directories
foreach(CONFIG_TYPE ${CMAKE_CONFIGURATION_TYPES})
    string(TOUPPER ${CONFIG_TYPE} CONFIG_TYPE_UPPER)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${CONFIG_TYPE_UPPER} ${CMAKE_BINARY_DIR}/bin/${CONFIG_TYPE})
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_${CONFIG_TYPE_UPPER} ${CMAKE_BINARY_DIR}/lib/${CONFIG_TYPE})
    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_${CONFIG_TYPE_UPPER} ${CMAKE_BINARY_DIR}/lib/${CONFIG_TYPE})
endforeach()

# -----------------------------------------------------------------------------
# Global Compiler Options
# -----------------------------------------------------------------------------
if(MSVC)
    add_compile_options(
        /W4                 # Warning level 4
        /WX                 # Treat warnings as errors
        /MP                 # Multi-processor compilation
        /permissive-        # Strict conformance mode
        /Zc:__cplusplus     # Correct __cplusplus macro
        /Zc:preprocessor    # Standard-conforming preprocessor
        /utf-8              # Source and execution charset
    )
    add_compile_definitions(
        _CRT_SECURE_NO_WARNINGS
        NOMINMAX            # Prevent Windows.h min/max macros
        WIN32_LEAN_AND_MEAN # Minimal Windows headers
    )
else()
    add_compile_options(
        -Wall
        -Wextra
        -Wpedantic
        -Werror
    )
endif()

# -----------------------------------------------------------------------------
# Build Type Configuration
# -----------------------------------------------------------------------------
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE "Debug" CACHE STRING "Build type" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "Debug" "Release" "RelWithDebInfo" "Profile")
endif()

# Custom Profile configuration (Release + Debug symbols)
set(CMAKE_CXX_FLAGS_PROFILE "${CMAKE_CXX_FLAGS_RELWITHDEBINFO}" CACHE STRING "" FORCE)
set(CMAKE_C_FLAGS_PROFILE "${CMAKE_C_FLAGS_RELWITHDEBINFO}" CACHE STRING "" FORCE)
set(CMAKE_EXE_LINKER_FLAGS_PROFILE "${CMAKE_EXE_LINKER_FLAGS_RELWITHDEBINFO}" CACHE STRING "" FORCE)

# -----------------------------------------------------------------------------
# Platform Detection
# -----------------------------------------------------------------------------
if(WIN32)
    set(DOT_PLATFORM_WINDOWS ON)
    add_compile_definitions(DOT_PLATFORM_WINDOWS=1)
elseif(APPLE)
    set(DOT_PLATFORM_MACOS ON)
    add_compile_definitions(DOT_PLATFORM_MACOS=1)
elseif(UNIX)
    set(DOT_PLATFORM_LINUX ON)
    add_compile_definitions(DOT_PLATFORM_LINUX=1)
endif()

# Debug/Release definitions
add_compile_definitions(
    $<$<CONFIG:Debug>:DOT_DEBUG=1>
    $<$<CONFIG:Release>:DOT_RELEASE=1>
    $<$<CONFIG:RelWithDebInfo>:DOT_RELEASE=1>
    $<$<CONFIG:Profile>:DOT_PROFILE=1>
)

# -----------------------------------------------------------------------------
# Options
# -----------------------------------------------------------------------------
option(DOT_BUILD_TESTS "Build unit tests" ON)
option(DOT_BUILD_EDITOR "Build the editor application" ON)
option(DOT_BUILD_GAME "Build the standalone game application" ON)

# -----------------------------------------------------------------------------
# Subdirectories
# -----------------------------------------------------------------------------
add_subdirectory(ThirdParty)
add_subdirectory(Engine)

if(WIN32)
    add_subdirectory(CrashReporter)
endif()

if(DOT_BUILD_EDITOR)
    add_subdirectory(Editor)
    if(WIN32)
        add_subdirectory(Tools)
    endif()
endif()

if(DOT_BUILD_GAME)
    add_subdirectory(Game)
endif()

if(DOT_BUILD_TESTS)
    enable_testing()
    add_subdirectory(Tests)
endif()

# -----------------------------------------------------------------------------
# Summary
# -----------------------------------------------------------------------------
message(STATUS "")
message(STATUS "=== Dot Engine Configuration ===")
message(STATUS "  Version:       ${PROJECT_VERSION}")
message(STATUS "  Platform:      ${CMAKE_SYSTEM_NAME}")
message(STATUS "  Compiler:      ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "  Build Type:    ${CMAKE_BUILD_TYPE}")
message(STATUS "  C++ Standard:  ${CMAKE_CXX_STANDARD}")
message(STATUS "  Build Tests:   ${DOT_BUILD_TESTS}")
message(STATUS "  Build Editor:  ${DOT_BUILD_EDITOR}")
message(STATUS "  Build Game:    ${DOT_BUILD_GAME}")
message(STATUS "================================")
message(STATUS "")
