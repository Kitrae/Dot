# DotRHI - Render Hardware Interface

file(GLOB_RECURSE RHI_HEADERS "Public/*.h")

# D3D12 Backend (Windows only)
if(WIN32)
    set(D3D12_SOURCES
        Private/D3D12/D3D12Common.h
        Private/D3D12/D3D12Device.h
        Private/D3D12/D3D12Device.cpp
        Private/D3D12/D3D12Buffer.h
        Private/D3D12/D3D12Buffer.cpp
        Private/D3D12/D3D12Texture.h
        Private/D3D12/D3D12Texture.cpp
        Private/D3D12/D3D12Shader.h
        Private/D3D12/D3D12Shader.cpp
        Private/D3D12/D3D12SwapChain.h
        Private/D3D12/D3D12SwapChain.cpp
        Private/D3D12/D3D12GUI.h
        Private/D3D12/D3D12GUI.cpp
        Private/D3D12/D3D12Pipeline.h
        Private/D3D12/D3D12Pipeline.cpp
    )
endif()

add_library(DotRHI STATIC
    ${RHI_HEADERS}
    Private/RHI.cpp
    Private/FrameGraph.cpp
    ${D3D12_SOURCES}
)

add_library(Dot::RHI ALIAS DotRHI)

target_include_directories(DotRHI 
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/Public
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/Private
)

target_link_libraries(DotRHI PUBLIC DotCore)

# D3D12 libs and ImGui on Windows
if(WIN32)
    target_link_libraries(DotRHI PRIVATE d3d12 dxgi d3dcompiler)
    if(TARGET imgui)
        target_link_libraries(DotRHI PRIVATE imgui)
    endif()
endif()

set_target_properties(DotRHI PROPERTIES
    FOLDER "Engine"
)

