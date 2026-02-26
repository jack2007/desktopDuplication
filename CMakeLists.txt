
#运行指令
#cmake -S . -B build -DCMAKE_PREFIX_PATH=%cd%/vcpkg_installed/x64-windows

cmake_minimum_required(VERSION 3.15)
project(DesktopDuplication)

if(MSVC)
    # 添加编译选项到所有目标
    add_compile_options(/utf-8)
    
    # 可选：同时设置字符集为 Unicode（等价于 VS 里的“使用 Unicode 字符集”）
    add_definitions(-DUNICODE -D_UNICODE -D_DISABLE_CONSTEXPR_MUTEX_CONSTRUCTOR)
endif()

set(CMAKE_CXX_STANDARD 17)

# 自动补充项目内 vcpkg 依赖路径，避免每次手动传 -DCMAKE_PREFIX_PATH
set(LOCAL_VCPKG_PREFIX "${CMAKE_CURRENT_SOURCE_DIR}/vcpkg_installed/x64-windows")
if(EXISTS "${LOCAL_VCPKG_PREFIX}/share/zstd/zstdConfig.cmake")
    list(PREPEND CMAKE_PREFIX_PATH "${LOCAL_VCPKG_PREFIX}")
endif()

set(ZSTD_CONFIG_FILE "${LOCAL_VCPKG_PREFIX}/share/zstd/zstdConfig.cmake")
set(ZSTD_INCLUDE_FILE "${LOCAL_VCPKG_PREFIX}/include/zstd.h")
set(ZSTD_RELEASE_LIB "${LOCAL_VCPKG_PREFIX}/lib/zstd.lib")
set(ZSTD_DEBUG_LIB "${LOCAL_VCPKG_PREFIX}/debug/lib/zstd.lib")

# 若 zstd 的 debug 包缺失，find_package(zstd CONFIG) 会在配置阶段报错。
# 这种情况下回退到手动导入 release 库，保证 CMake 能正常生成工程文件。
if(EXISTS "${ZSTD_CONFIG_FILE}" AND EXISTS "${ZSTD_DEBUG_LIB}")
    find_package(zstd CONFIG REQUIRED)
else()
    if(NOT EXISTS "${ZSTD_RELEASE_LIB}" OR NOT EXISTS "${ZSTD_INCLUDE_FILE}")
        message(FATAL_ERROR "zstd not found. Expected files under ${LOCAL_VCPKG_PREFIX}")
    endif()

    add_library(zstd::libzstd UNKNOWN IMPORTED)
    set_target_properties(zstd::libzstd PROPERTIES
        IMPORTED_LOCATION "${ZSTD_RELEASE_LIB}"
        IMPORTED_LOCATION_DEBUG "${ZSTD_RELEASE_LIB}"
        IMPORTED_LOCATION_RELEASE "${ZSTD_RELEASE_LIB}"
        INTERFACE_INCLUDE_DIRECTORIES "${LOCAL_VCPKG_PREFIX}/include"
    )
endif()
#find_package(spdlog CONFIG REQUIRED)

# Common Library
add_library(Common STATIC
    Common/TcpServer.cpp
    Common/TcpClient.cpp
    Common/ZstdCompressor.cpp
)
#target_link_libraries(Common PUBLIC zstd::libzstd ws2_32 spdlog::spdlog)
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
