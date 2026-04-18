# =============================================================================
# Dot Engine - Crash Reporter
# =============================================================================

add_executable(DotCrashReporter WIN32
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/CrashReporterMain.cpp
)

target_include_directories(DotCrashReporter
    PRIVATE
        ${CMAKE_SOURCE_DIR}/Engine/Core/Public
)

target_link_libraries(DotCrashReporter
    PRIVATE
        Dot::Core
)

if(WIN32)
    target_link_libraries(DotCrashReporter PRIVATE user32 shell32)
endif()

set_target_properties(DotCrashReporter PROPERTIES
    FOLDER "Tools"
)
