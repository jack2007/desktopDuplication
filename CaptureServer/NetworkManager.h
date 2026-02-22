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
#include <d3dcompiler.h>
#include <vector>

class NetworkManager
{
public:
    NetworkManager();
    ~NetworkManager();

    bool Initialize(int port, unsigned int compressLevel);
    bool WaitForClient();
    bool SendInitPacket(UINT32 width, UINT32 height, DXGI_FORMAT format);
    bool SendFramePacket(const FRAME_DATA* data, const PTR_INFO* ptrInfo, ID3D11Device* device, ID3D11DeviceContext* context);
    bool IsConnected();
    void Disconnect();

private:
    // Compile and create the compute shader; called lazily on first use.
    bool InitializeComputeShader(ID3D11Device* device);

    // Release all compute-shader GPU resources.
    void CleanupComputeResources();

    // Extract pixels for the given dirty rects using the GPU compute shader.
    // Replaces the old full-frame CopyResource path.
    bool ReadPixelsFromGPU(ID3D11Texture2D* srcTexture, ID3D11Device* device, ID3D11DeviceContext* context, const RECT* dirtyRects, UINT dirtyCount, std::vector<BYTE>& outPixels);

    TcpServer      m_Server;
    ZstdCompressor m_Compressor;

    // ------- Compute-shader resources ----------------------------------------
    ID3D11ComputeShader*       m_ComputeShader;
    // StructuredBuffer<DirtyRectInfo> – updated each frame with rect data.
    ID3D11Buffer*              m_DirtyRectBuffer;
    ID3D11ShaderResourceView*  m_DirtyRectSRV;
    // RWByteAddressBuffer – receives densely-packed pixel output.
    ID3D11Buffer*              m_OutputBuffer;
    ID3D11UnorderedAccessView* m_OutputUAV;
    // Staging buffer for CPU readback of the output buffer.
    ID3D11Buffer*              m_OutputStagingBuffer;
    // Constant buffer holding DirtyRectCount for the shader.
    ID3D11Buffer*              m_ConstantBuffer;
    // Current allocated capacity of the output / staging buffers (in pixels).
    UINT                       m_OutputCapacityPixels;
    // Current allocated capacity of the dirty-rect buffer (in elements).
    UINT                       m_DirtyRectCapacity;
    // -------------------------------------------------------------------------

    bool   m_NeedsFullFrame;
    UINT32 m_ScreenWidth;
    UINT32 m_ScreenHeight;
};

#endif
