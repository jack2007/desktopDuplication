# DesktopDuplication

基于 Windows Desktop Duplication API 的远程桌面捕获与显示系统。

[English Version](./README.md)

## 项目架构

该项目包含两个核心组件：

- **CaptureServer** - 屏幕捕获服务端
  - 使用 Windows DXGI Desktop Duplication API 捕获屏幕
  - 使用 Zstd 算法压缩帧数据
  - 通过 TCP 协议向客户端传输数据

- **DisplayClient** - 客户端显示程序
  - 接收服务端传输的压缩帧数据
  - 解压并通过 Direct3D 11 渲染显示

## 技术栈

- **语言**: C++17
- **图形API**: Direct3D 11, DXGI
- **网络**: Winsock2 (TCP)
- **压缩**: Zstandard (zstd)
- **构建系统**: CMake
- **依赖管理**: vcpkg

## 构建要求

- Windows 10/11
- Visual Studio 2019 或更高版本
- CMake 3.15+
- vcpkg

## 构建步骤

```bash
# 1. 配置 CMake（使用 vcpkg）
cmake -S . -B build -DCMAKE_PREFIX_PATH=%cd%/vcpkg_installed/x64-windows

# 2. 编译项目
cmake --build build --config Release
```

## 使用方法

### 启动 CaptureServer（服务端）

```bash
CaptureServer.exe [选项]
```

选项：
- `-output <index>` - 指定输出设备索引（使用数字指定单个输出，或 "all" 捕获所有输出，默认：-1 表示第一个输出）
- `-port <端口>` - 指定监听端口（默认：9000）
- `-fps <帧率>` - 目标帧率（默认：0 表示无限制）
- `-compress <级别>` - 压缩级别 0-9（默认：0）

示例：
```bash
CaptureServer.exe -output 0 -port 9000 -fps 60 -compress 5
CaptureServer.exe -output all -port 9000
```

### 启动 DisplayClient（客户端）

```bash
DisplayClient.exe [选项]
```

选项：
- `-server <IP地址>` - 指定服务器 IP 地址（必填）
- `-port <端口>` - 指定服务器端口（默认：9000）

示例：
```bash
DisplayClient.exe -server 192.168.1.100 -port 9000
```

## 项目结构

```
├── CaptureServer/              # 屏幕捕获服务端源码
│   ├── DesktopDuplication.cpp
│   ├── DisplayManager.cpp
│   ├── DisplayManager.h
│   ├── DuplicationManager.cpp
│   ├── DuplicationManager.h
│   ├── NetworkManager.cpp
│   ├── NetworkManager.h
│   ├── OutputManager.cpp
│   ├── OutputManager.h
│   ├── ThreadManager.cpp
│   └── ThreadManager.h
├── DisplayClient/              # 客户端显示程序源码
│   ├── DesktopDuplication.cpp
│   ├── ClientLogic.cpp
│   ├── ClientLogic.h
│   ├── DisplayManager.cpp
│   ├── DisplayManager.h
│   ├── DuplicationManager.cpp
│   ├── DuplicationManager.h
│   ├── NetworkClient.cpp
│   ├── NetworkClient.h
│   ├── OutputManager.cpp
│   ├── OutputManager.h
│   ├── ThreadManager.cpp
│   └── ThreadManager.h
├── Common/                     # 公共库源码
│   ├── CommonTypes.h
│   ├── NetworkProtocol.h
│   ├── PixelShader.hlsl
│   ├── TcpClient.cpp
│   ├── TcpClient.h
│   ├── TcpServer.cpp
│   ├── TcpServer.h
│   ├── VertexShader.hlsl
│   ├── ZstdCompressor.cpp
│   └── ZstdCompressor.h
├── CMakeLists.txt
└── vcpkg.json
```

## 许可证

LGPL（GNU Lesser General Public License）
