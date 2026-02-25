// THIS CODE AND INFORMATION IS PROVIDED "AS IS" WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING BUT NOT LIMITED TO
// THE IMPLIED WARRANTIES OF MERCHANTABILITY AND/OR FITNESS FOR A
// PARTICULAR PURPOSE.
//
// Copyright (c) Microsoft Corporation. All rights reserved

#ifndef _CLIENTLOGIC_H_
#define _CLIENTLOGIC_H_

#include <winsock2.h>
#include <ws2tcpip.h>
#include <string>
#include <unordered_map>
#include <vector>
#include "CommonTypes.h"
#include "NetworkClient.h"
#include "DisplayManager.h"
#include "OutputManager.h"

struct CachedCursor
{
    DXGI_OUTDUPL_POINTER_SHAPE_INFO ShapeInfo;
    std::vector<BYTE> ShapeBuffer;
};

class ClientLogic
{
public:
    ClientLogic();
    ~ClientLogic();

    DUPL_RETURN Initialize(HWND windowHandle, const char* serverIP, int port);
    void RunLoop();
    void Clean();
    void WindowResize();

    // Mouse event handlers called from WndProc
    void OnMouseMove(int clientX, int clientY);
    void OnMouseButton(MouseInputType type, int clientX, int clientY);
    void OnMouseWheel(int delta, int clientX, int clientY);

private:
    DUPL_RETURN InitializeDx();
    void CleanDx();
    bool UpdateLocalTexture(const std::vector<BYTE>& pixelData, const RECT* dirtyRects, UINT dirtyCount);
    void UpdateWindowTitle(float fps);
    void MapClientToServer(int clientX, int clientY, INT32& serverX, INT32& serverY);
    void ProcessCursorShape(const CursorShapePacket& packet, const std::vector<BYTE>& shapeData);

    NetworkClient m_NetClient;
    DISPLAYMANAGER m_DispMgr;
    OUTPUTMANAGER m_OutMgr;
    
    DX_RESOURCES m_DxRes;
    ID3D11Texture2D* m_LocalTexture;
    ID3D11Texture2D* m_SharedSurf;
    IDXGIKeyedMutex* m_KeyMutex;
    
    HWND m_WindowHandle;
    InitPacket m_InitData;
    PTR_INFO m_PtrInfo;
    bool m_Occluded;
    std::vector<BYTE> m_PrevFrame;  // previous frame buffer for XOR delta decoding

    // FPS tracking
    DWORD m_FrameCount;
    DWORD m_LastFPSTick;

    // Connection info for window title
    std::string m_ServerIP;
    int m_ServerPort;

    // Mouse control
    DWORD m_LastMouseSendTick;
    std::unordered_map<UINT32, CachedCursor> m_CursorCache;
};

#endif
