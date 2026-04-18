# =============================================================================
# Dot Engine - DotMcpServer
# =============================================================================

find_program(NPM_EXECUTABLE npm REQUIRED)

add_custom_target(DotMcpServerDeps
    COMMAND ${NPM_EXECUTABLE} install
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMENT "Installing DotMcpServer Node dependencies"
)

add_executable(DotMcpServer
    main.cpp
)

add_dependencies(DotMcpServer DotMcpServerDeps)

if(MSVC)
    target_compile_options(DotMcpServer PRIVATE /bigobj)
endif()

set_target_properties(DotMcpServer PROPERTIES
    FOLDER "Tools"
)
