
#运行指令
#cmake -S . -B build -DCMAKE_PREFIX_PATH=%cd%/vcpkg_installed/x64-windows

cmake_minimum_required(VERSION 3.15)
project(DesktopDuplication)

set(CMAKE_CXX_STANDARD 17)

find_package(zstd CONFIG REQUIRED)

# Common Library
add_library(Common STATIC
    Common/TcpServer.cpp
    Common/TcpClient.cpp
    Common/ZstdCompressor.cpp
)
target_link_libraries(Common PUBLIC zstd::libzstd ws2_32)
target_include_directories(Common PUBLIC Common)

# Capture Server
add_executable(CaptureServer
    CaptureServer/DesktopDuplication.cpp
    CaptureServer/DisplayManager.cpp
    CaptureServer/DuplicationManager.cpp
    CaptureServer/OutputManager.cpp
    CaptureServer/ThreadManager.cpp
    CaptureServer/NetworkManager.cpp
    CaptureServer/MouseController.cpp
)
target_link_libraries(CaptureServer PRIVATE Common d3d11 dxgi)

# Display Client
add_executable(DisplayClient WIN32
    DisplayClient/DesktopDuplication.cpp
    DisplayClient/DisplayManager.cpp
    DisplayClient/OutputManager.cpp
    DisplayClient/ClientLogic.cpp
    DisplayClient/NetworkClient.cpp
)
target_link_libraries(DisplayClient PRIVATE Common d3d11 dxgi)
set_target_properties(DisplayClient PROPERTIES VS_GLOBAL_KEYWORD "Win32Proj")

# Custom command to compile HLSL shaders
function(compile_shader TARGET SHADER_FILE ENTRY_POINT PROFILE HEADER_FILE)
    add_custom_command(
        TARGET ${TARGET} PRE_BUILD
        COMMAND fxc.exe /nologo /E ${ENTRY_POINT} /T ${PROFILE} /Vn g_${ENTRY_POINT} /Fh ${CMAKE_CURRENT_BINARY_DIR}/${HEADER_FILE} ${CMAKE_CURRENT_SOURCE_DIR}/${SHADER_FILE}
        BYPRODUCTS ${CMAKE_CURRENT_BINARY_DIR}/${HEADER_FILE}
    )
    target_include_directories(${TARGET} PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
endfunction()

compile_shader(CaptureServer Common/VertexShader.hlsl VS vs_4_0 VertexShader.h)
compile_shader(CaptureServer Common/PixelShader.hlsl PS ps_4_0 PixelShader.h)

compile_shader(DisplayClient Common/VertexShader.hlsl VS vs_4_0 VertexShader.h)
compile_shader(DisplayClient Common/PixelShader.hlsl PS ps_4_0 PixelShader.h)
