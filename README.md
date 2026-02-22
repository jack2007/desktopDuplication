# DesktopDuplication

Remote desktop capture and display system based on Windows Desktop Duplication API.

[中文版](./README_cn.md)

## Project Architecture

This project consists of two core components:

- **CaptureServer** - Screen capture server
  - Captures screen using Windows DXGI Desktop Duplication API
  - Compresses frame data using Zstd algorithm
  - Transmits data to clients via TCP protocol

- **DisplayClient** - Client display application
  - Receives compressed frame data from server
  - Decompresses and renders using Direct3D 11

## Technology Stack

- **Language**: C++17
- **Graphics API**: Direct3D 11, DXGI
- **Network**: Winsock2 (TCP)
- **Compression**: Zstandard (zstd)
- **Build System**: CMake
- **Dependency Management**: vcpkg

## Build Requirements

- Windows 10/11
- Visual Studio 2019 or higher
- CMake 3.15+
- vcpkg

## Build Steps

```bash
# 1. Configure CMake (using vcpkg)
cmake -S . -B build -DCMAKE_PREFIX_PATH=%cd%/vcpkg_installed/x64-windows

# 2. Build project
cmake --build build --config Release
```

## Usage

### Start CaptureServer (Server)

```bash
CaptureServer.exe [options]
```

Options:
- `-port <port>` - Specify listening port (default: 9000)
- `-fps <fps>` - Target frame rate (default: 0 means unlimited)
- `-compress <level>` - Compression level 0-9 (default: 0)

Examples:
```bash
CaptureServer.exe -port 9000 -fps 60 -compress 5
CaptureServer.exe -port 9000
```

### Start DisplayClient (Client)

```bash
DisplayClient.exe [options]
```

Options:
- `-server <IP address>` - Specify server IP address (required)
- `-port <port>` - Specify server port (default: 9000)

Examples:
```bash
DisplayClient.exe -server 192.168.1.100 -port 9000
```

## Project Structure

```
├── CaptureServer/              # Screen capture server source
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
├── DisplayClient/              # Client display application source
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
├── Common/                     # Common library source
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

## License

LGPL (GNU Lesser General Public License)
