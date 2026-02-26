// THIS CODE AND INFORMATION IS PROVIDED "AS IS" WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING BUT NOT LIMITED TO
// THE IMPLIED WARRANTIES OF MERCHANTABILITY AND/OR FITNESS FOR A
// PARTICULAR PURPOSE.
//
// Copyright (c) Microsoft Corporation. All rights reserved

#ifndef _NETWORKMANAGER_H_
#define _NETWORKMANAGER_H_

#include "../Common/TcpServer.h"
#include "../Common/ZstdCompressor.h"
#include "../Common/NetworkProtocol.h"
#include "CommonTypes.h"
#include "MouseController.h"
#include "KeyboardController.h"
#include <vector>

class NetworkManager
{
public:
    NetworkManager();
    ~NetworkManager();

    bool Initialize(int port, unsigned int compressLevel);
    bool WaitForClient();
    bool SendInitPacket(UINT32 width, UINT32 height, DXGI_FORMAT format, INT32 desktopLeft = 0, INT32 desktopTop = 0);
    bool SendFramePacket(const FRAME_DATA* data, const PTR_INFO* ptrInfo, ID3D11Device* device, ID3D11DeviceContext* context);
    bool IsConnected();
    void Disconnect();

    bool HasClientData();
    bool ReceiveMouseInput(MouseInputPacket& outInput);
    bool ReceiveKeyboardInput(KeyboardInputPacket& outInput);
    bool SendCursorShape(const CursorShapePacket& packet, const std::vector<BYTE>& shapeData);
    void ProcessPendingMouseInput();

private:
    bool ReadPixelsFromGPU(ID3D11Texture2D* srcTexture, ID3D11Device* device, ID3D11DeviceContext* context, const RECT* dirtyRects, UINT dirtyCount, std::vector<BYTE>& outPixels);

    TcpServer      m_Server;
    ZstdCompressor m_Compressor;
    MouseController m_MouseController;
    KeyboardController m_KeyboardController;
    ID3D11Texture2D* m_StagingTexture;
    D3D11_TEXTURE2D_DESC m_StagingDesc;
    bool m_NeedsFullFrame;
    UINT32 m_ScreenWidth;
    UINT32 m_ScreenHeight;
    std::vector<BYTE> m_PrevFrame;  // previous frame buffer for inter-frame XOR diff
};

#endif
