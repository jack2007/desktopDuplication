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
#include "CommonTypes.h"
#include "NetworkClient.h"
#include "DisplayManager.h"
#include "OutputManager.h"

class ClientLogic
{
public:
    ClientLogic();
    ~ClientLogic();

    DUPL_RETURN Initialize(HWND windowHandle);
    void RunLoop();
    void Clean();

private:
    DUPL_RETURN InitializeDx();
    void CleanDx();
    bool UpdateLocalTexture(const std::vector<BYTE>& pixelData, const RECT* dirtyRects, UINT dirtyCount);

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
};

#endif
