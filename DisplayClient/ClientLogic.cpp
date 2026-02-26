// THIS CODE AND INFORMATION IS PROVIDED "AS IS" WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING BUT NOT LIMITED TO
// THE IMPLIED WARRANTIES OF MERCHANTABILITY AND/OR FITNESS FOR A
// PARTICULAR PURPOSE.
//
// Copyright (c) Microsoft Corporation. All rights reserved

#include "ClientLogic.h"
#include <emmintrin.h>
#include <iostream>
#include "Logger.h"

// Include shaders only once in the cpp file that needs them
#include "VertexShader.h"
#include "PixelShader.h"

ClientLogic::ClientLogic() : m_LocalTexture(nullptr), m_SharedSurf(nullptr), m_KeyMutex(nullptr), m_WindowHandle(nullptr), m_Occluded(false), m_FrameCount(0), m_LastFPSTick(0), m_ServerPort(0), m_LastMouseSendTick(0), m_HasRemoteCursorShape(false)
{
    RtlZeroMemory(&m_DxRes, sizeof(m_DxRes));
    RtlZeroMemory(&m_InitData, sizeof(m_InitData));
    RtlZeroMemory(&m_PtrInfo, sizeof(m_PtrInfo));
}

ClientLogic::~ClientLogic()
{
    Clean();
}

void ClientLogic::Clean()
{
    m_NetClient.Disconnect();
    m_DispMgr.CleanRefs();
    m_OutMgr.CleanRefs();

    if (m_LocalTexture)
    {
        m_LocalTexture->Release();
        m_LocalTexture = nullptr;
    }

    if (m_SharedSurf)
    {
        m_SharedSurf->Release();
        m_SharedSurf = nullptr;
    }

    if (m_KeyMutex)
    {
        m_KeyMutex->Release();
        m_KeyMutex = nullptr;
    }

    if (m_PtrInfo.PtrShapeBuffer)
    {
        delete[] m_PtrInfo.PtrShapeBuffer;
        m_PtrInfo.PtrShapeBuffer = nullptr;
    }
    m_PtrInfo.Visible = false;
    m_PtrInfo.BufferSize = 0;
    m_CursorCache.clear();
    m_LastMouseSendTick = 0;
    m_HasRemoteCursorShape = false;
    m_Occluded = false;
    m_PrevFrame.clear();

    CleanDx();
}

void ClientLogic::CleanDx()
{
    if (m_DxRes.Device)
    {
        m_DxRes.Device->Release();
        m_DxRes.Device = nullptr;
    }

    if (m_DxRes.Context)
    {
        m_DxRes.Context->Release();
        m_DxRes.Context = nullptr;
    }

    if (m_DxRes.VertexShader)
    {
        m_DxRes.VertexShader->Release();
        m_DxRes.VertexShader = nullptr;
    }

    if (m_DxRes.PixelShader)
    {
        m_DxRes.PixelShader->Release();
        m_DxRes.PixelShader = nullptr;
    }

    if (m_DxRes.InputLayout)
    {
        m_DxRes.InputLayout->Release();
        m_DxRes.InputLayout = nullptr;
    }

    if (m_DxRes.SamplerLinear)
    {
        m_DxRes.SamplerLinear->Release();
        m_DxRes.SamplerLinear = nullptr;
    }
}

void ClientLogic::WindowResize()
{
    m_OutMgr.WindowResize();
}

void ClientLogic::UpdateWindowTitle(float fps)
{
    wchar_t title[256];
    swprintf_s(title, L"Desktop Duplication Client | %hs:%d | FPS: %.1f",
               m_ServerIP.c_str(), m_ServerPort, fps);
    SetWindowTextW(m_WindowHandle, title);
}

DUPL_RETURN ClientLogic::InitializeDx()
{
    HRESULT hr = S_OK;

    D3D_DRIVER_TYPE DriverTypes[] =
    {
        D3D_DRIVER_TYPE_HARDWARE,
        D3D_DRIVER_TYPE_WARP,
        D3D_DRIVER_TYPE_REFERENCE,
    };
    UINT NumDriverTypes = ARRAYSIZE(DriverTypes);

    D3D_FEATURE_LEVEL FeatureLevels[] =
    {
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1,
        D3D_FEATURE_LEVEL_10_0,
        D3D_FEATURE_LEVEL_9_1
    };
    UINT NumFeatureLevels = ARRAYSIZE(FeatureLevels);

    D3D_FEATURE_LEVEL FeatureLevel;

    for (UINT DriverTypeIndex = 0; DriverTypeIndex < NumDriverTypes; ++DriverTypeIndex)
    {
        hr = D3D11CreateDevice(nullptr, DriverTypes[DriverTypeIndex], nullptr, 0, FeatureLevels, NumFeatureLevels,
                                D3D11_SDK_VERSION, &m_DxRes.Device, &FeatureLevel, &m_DxRes.Context);
        if (SUCCEEDED(hr))
        {
            break;
        }
    }
    if (FAILED(hr))
    {
        return ProcessFailure(nullptr, L"Failed to create device in InitializeDx", L"Error", hr);
    }

    UINT Size = ARRAYSIZE(g_VS);
    hr = m_DxRes.Device->CreateVertexShader(g_VS, Size, nullptr, &m_DxRes.VertexShader);
    if (FAILED(hr))
    {
        return ProcessFailure(m_DxRes.Device, L"Failed to create vertex shader in InitializeDx", L"Error", hr, SystemTransitionsExpectedErrors);
    }

    D3D11_INPUT_ELEMENT_DESC Layout[] =
    {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0}
    };
    UINT NumElements = ARRAYSIZE(Layout);
    hr = m_DxRes.Device->CreateInputLayout(Layout, NumElements, g_VS, Size, &m_DxRes.InputLayout);
    if (FAILED(hr))
    {
        return ProcessFailure(m_DxRes.Device, L"Failed to create input layout in InitializeDx", L"Error", hr, SystemTransitionsExpectedErrors);
    }
    m_DxRes.Context->IASetInputLayout(m_DxRes.InputLayout);

    Size = ARRAYSIZE(g_PS);
    hr = m_DxRes.Device->CreatePixelShader(g_PS, Size, nullptr, &m_DxRes.PixelShader);
    if (FAILED(hr))
    {
        return ProcessFailure(m_DxRes.Device, L"Failed to create pixel shader in InitializeDx", L"Error", hr, SystemTransitionsExpectedErrors);
    }

    D3D11_SAMPLER_DESC SampDesc;
    RtlZeroMemory(&SampDesc, sizeof(SampDesc));
    SampDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    SampDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    SampDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    SampDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    SampDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
    SampDesc.MinLOD = 0;
    SampDesc.MaxLOD = D3D11_FLOAT32_MAX;
    hr = m_DxRes.Device->CreateSamplerState(&SampDesc, &m_DxRes.SamplerLinear);
    if (FAILED(hr))
    {
        return ProcessFailure(m_DxRes.Device, L"Failed to create sampler state in InitializeDx", L"Error", hr, SystemTransitionsExpectedErrors);
    }

    return DUPL_RETURN_SUCCESS;
}

DUPL_RETURN ClientLogic::Initialize(HWND windowHandle, const char* serverIP, int port)
{
    LOG_INFO("ClientLogic::Initialize server={}:{}", serverIP ? serverIP : "(null)", port);
    m_WindowHandle = windowHandle;
    m_ServerIP = serverIP ? serverIP : "127.0.0.1";
    m_ServerPort = port;
    m_Occluded = false;

    if (!m_NetClient.Initialize(m_ServerIP.c_str(), m_ServerPort))
    {
        LOG_ERROR("ClientLogic::Initialize: failed to connect to server {}:{}", m_ServerIP, m_ServerPort);
        return ProcessFailure(nullptr, L"Failed to connect to server", L"Error", E_FAIL);
    }

    if (!m_NetClient.ReceiveInitPacket(m_InitData))
    {
        LOG_ERROR("ClientLogic::Initialize: failed to receive init packet");
        return ProcessFailure(nullptr, L"Failed to receive init packet", L"Error", E_FAIL);
    }

    LOG_INFO("ClientLogic::Initialize: received init packet width={}, height={}", m_InitData.Width, m_InitData.Height);

    DUPL_RETURN Ret = InitializeDx();
    if (Ret != DUPL_RETURN_SUCCESS) return Ret;

    m_DispMgr.InitD3D(&m_DxRes);

    UINT OutCount;
    RECT DeskBounds = { 0, 0, static_cast<LONG>(m_InitData.Width), static_cast<LONG>(m_InitData.Height) };
    Ret = m_OutMgr.InitOutput(m_WindowHandle, 0, &OutCount, &DeskBounds);
    if (Ret != DUPL_RETURN_SUCCESS) return Ret;

    HANDLE SharedHandle = m_OutMgr.GetSharedHandle();
    if (!SharedHandle)
    {
        return ProcessFailure(nullptr, L"Failed to get shared handle", L"Error", E_FAIL);
    }

    HRESULT hr = m_DxRes.Device->OpenSharedResource(SharedHandle, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&m_SharedSurf));
    if (FAILED(hr))
    {
        return ProcessFailure(m_DxRes.Device, L"Opening shared texture failed", L"Error", hr, SystemTransitionsExpectedErrors);
    }

    hr = m_SharedSurf->QueryInterface(__uuidof(IDXGIKeyedMutex), reinterpret_cast<void**>(&m_KeyMutex));
    if (FAILED(hr))
    {
        return ProcessFailure(nullptr, L"Failed to get keyed mutex interface", L"Error", hr);
    }

    D3D11_TEXTURE2D_DESC desc;
    RtlZeroMemory(&desc, sizeof(desc));
    desc.Width = m_InitData.Width;
    desc.Height = m_InitData.Height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = m_InitData.Format;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    desc.CPUAccessFlags = 0;

    hr = m_DxRes.Device->CreateTexture2D(&desc, nullptr, &m_LocalTexture);
    if (FAILED(hr))
    {
        return ProcessFailure(m_DxRes.Device, L"Failed to create local texture", L"Error", hr);
    }

    LOG_INFO("ClientLogic::Initialize: succeeded");
    return DUPL_RETURN_SUCCESS;
}

bool ClientLogic::UpdateLocalTexture(const std::vector<BYTE>& pixelData, const RECT* dirtyRects, UINT dirtyCount)
{
    if (!m_LocalTexture || pixelData.empty() || dirtyCount == 0) return true;

    UINT bytesPerPixel = 4;
    size_t offset = 0;

    for (UINT i = 0; i < dirtyCount; ++i)
    {
        UINT width = dirtyRects[i].right - dirtyRects[i].left;
        UINT height = dirtyRects[i].bottom - dirtyRects[i].top;
        UINT rowPitch = width * bytesPerPixel;
        UINT depthPitch = rowPitch * height;

        if (offset + depthPitch > pixelData.size()) return false;

        D3D11_BOX box;
        box.left = dirtyRects[i].left;
        box.right = dirtyRects[i].right;
        box.top = dirtyRects[i].top;
        box.bottom = dirtyRects[i].bottom;
        box.front = 0;
        box.back = 1;

        m_DxRes.Context->UpdateSubresource(m_LocalTexture, 0, &box, pixelData.data() + offset, rowPitch, depthPitch);
        offset += depthPitch;
    }

    return true;
}

void ClientLogic::MapClientToServer(int clientX, int clientY, INT32& serverX, INT32& serverY)
{
    RECT clientRect = {};
    GetClientRect(m_WindowHandle, &clientRect);
    int clientWidth  = clientRect.right  - clientRect.left;
    int clientHeight = clientRect.bottom - clientRect.top;

    if (clientWidth <= 0 || clientHeight <= 0 || m_InitData.Width == 0 || m_InitData.Height == 0)
    {
        serverX = 0;
        serverY = 0;
        return;
    }

    serverX = static_cast<INT32>(static_cast<long long>(clientX) * m_InitData.Width  / clientWidth);
    serverY = static_cast<INT32>(static_cast<long long>(clientY) * m_InitData.Height / clientHeight);

    serverX = max(0, min(serverX, static_cast<INT32>(m_InitData.Width)  - 1));
    serverY = max(0, min(serverY, static_cast<INT32>(m_InitData.Height) - 1));
}

void ClientLogic::ProcessCursorShape(const CursorShapePacket& packet, const std::vector<BYTE>& shapeData)
{
    if (packet.ShapeBufferSize > 0 && !shapeData.empty())
    {
        m_PtrInfo.ShapeInfo.Type        = packet.Type;
        m_PtrInfo.ShapeInfo.Width       = packet.Width;
        m_PtrInfo.ShapeInfo.Height      = packet.Height;
        m_PtrInfo.ShapeInfo.Pitch       = packet.Pitch;
        m_PtrInfo.ShapeInfo.HotSpot.x   = packet.HotspotX;
        m_PtrInfo.ShapeInfo.HotSpot.y   = packet.HotspotY;
        m_PtrInfo.Visible = true;

        if (m_PtrInfo.BufferSize < packet.ShapeBufferSize)
        {
            if (m_PtrInfo.PtrShapeBuffer) delete[] m_PtrInfo.PtrShapeBuffer;
            m_PtrInfo.PtrShapeBuffer = new BYTE[packet.ShapeBufferSize];
            m_PtrInfo.BufferSize = packet.ShapeBufferSize;
        }
        memcpy(m_PtrInfo.PtrShapeBuffer, shapeData.data(), packet.ShapeBufferSize);
        m_HasRemoteCursorShape = true;

        // Cache the cursor shape by ID
        CachedCursor cached;
        cached.ShapeInfo = m_PtrInfo.ShapeInfo;
        cached.ShapeBuffer.assign(shapeData.begin(), shapeData.begin() + packet.ShapeBufferSize);
        m_CursorCache[packet.CursorId] = std::move(cached);
    }
    else if (packet.ShapeBufferSize == 0)
    {
        // Look up cached shape by cursor ID
        auto it = m_CursorCache.find(packet.CursorId);
        if (it != m_CursorCache.end())
        {
            m_PtrInfo.ShapeInfo = it->second.ShapeInfo;
            m_PtrInfo.Visible = true;
            UINT32 bufSize = static_cast<UINT32>(it->second.ShapeBuffer.size());
            if (m_PtrInfo.BufferSize < bufSize)
            {
                if (m_PtrInfo.PtrShapeBuffer) delete[] m_PtrInfo.PtrShapeBuffer;
                m_PtrInfo.PtrShapeBuffer = new BYTE[bufSize];
                m_PtrInfo.BufferSize = bufSize;
            }
            memcpy(m_PtrInfo.PtrShapeBuffer, it->second.ShapeBuffer.data(), bufSize);
            m_HasRemoteCursorShape = true;
        }
    }
}

bool ClientLogic::ShouldHideLocalCursor() const
{
    // Only hide local cursor after we have a valid remote cursor shape.
    // This avoids a "cursor disappears completely" user experience if the
    // server never sends/returns cursor shape packets.
    return m_HasRemoteCursorShape && m_PtrInfo.Visible;
}

void ClientLogic::OnMouseMove(int clientX, int clientY)
{
    INT32 serverX, serverY;
    MapClientToServer(clientX, clientY, serverX, serverY);

    // Always update position for zero-latency display (Visible is set by ProcessCursorShape)
    m_PtrInfo.Position.x = serverX - m_PtrInfo.ShapeInfo.HotSpot.x;
    m_PtrInfo.Position.y = serverY - m_PtrInfo.ShapeInfo.HotSpot.y;

    // Immediately re-render the cursor at the new position without waiting for the
    // next server packet.  Use a non-blocking acquire (timeout=0): if the mutex is
    // free we do a lightweight release(key=1) + UpdateApplicationWindow cycle; if
    // it is busy the cursor will be drawn on the next regular frame render.
    if (m_PtrInfo.Visible && !m_Occluded && m_KeyMutex)
    {
        HRESULT hr = m_KeyMutex->AcquireSync(0, 0);
        if (SUCCEEDED(hr))
        {
            m_KeyMutex->ReleaseSync(1);
            m_OutMgr.UpdateApplicationWindow(&m_PtrInfo, &m_Occluded);
        }
    }

    // Throttle sends to ~500 Hz (every 2ms) to reduce control latency.
    DWORD now = GetTickCount();
    if (now - m_LastMouseSendTick < 2) return;
    m_LastMouseSendTick = now;

    MouseInputPacket input;
    input.InputType  = static_cast<UINT8>(MOUSE_INPUT_MOVE);
    input.X          = serverX;
    input.Y          = serverY;
    input.WheelDelta = 0;
    if (!m_NetClient.SendMouseInput(input))
    {
        LOG_WARN("ClientLogic::OnMouseMove: SendMouseInput failed");
    }
}

void ClientLogic::OnMouseButton(MouseInputType type, int clientX, int clientY)
{
    INT32 serverX, serverY;
    MapClientToServer(clientX, clientY, serverX, serverY);

    MouseInputPacket input;
    input.InputType  = static_cast<UINT8>(type);
    input.X          = serverX;
    input.Y          = serverY;
    input.WheelDelta = 0;
    if (!m_NetClient.SendMouseInput(input))
    {
        LOG_WARN("ClientLogic::OnMouseButton: SendMouseInput failed, type={}", static_cast<unsigned>(type));
    }
}

void ClientLogic::OnMouseWheel(int delta, int clientX, int clientY)
{
    INT32 serverX, serverY;
    MapClientToServer(clientX, clientY, serverX, serverY);

    MouseInputPacket input;
    input.InputType  = static_cast<UINT8>(MOUSE_INPUT_WHEEL);
    input.X          = serverX;
    input.Y          = serverY;
    input.WheelDelta = delta;
    if (!m_NetClient.SendMouseInput(input))
    {
        LOG_WARN("ClientLogic::OnMouseWheel: SendMouseInput failed, delta={}", delta);
    }
}

void ClientLogic::RunLoop()
{
    LOG_INFO("ClientLogic::RunLoop starting");
    MSG msg = { 0 };
    m_FrameCount = 0;
    m_LastFPSTick = GetTickCount();

    while (WM_QUIT != msg.message)
    {
        if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            if (msg.message == OCCLUSION_STATUS_MSG)
            {
                m_Occluded = false;
            }
            else
            {
                TranslateMessage(&msg);
                DispatchMessage(&msg);
            }
            continue;
        }

        if (!m_NetClient.HasData())
        {
            Sleep(1);
            continue;
        }

        PacketHeader pktHeader;
        if (!m_NetClient.ReceivePacketHeader(pktHeader))
        {
            LOG_WARN("ClientLogic::RunLoop: ReceivePacketHeader failed (connection lost)");
            break; // Connection closed or error
        }

        if (pktHeader.Type == PACKET_TYPE_CURSOR_SHAPE)
        {
            LOG_DEBUG("ClientLogic::RunLoop: received CURSOR_SHAPE packet");
            CursorShapePacket cursorPacket;
            std::vector<BYTE> shapeData;
            if (m_NetClient.ReceiveCursorShapeBody(pktHeader, cursorPacket, shapeData))
            {
                ProcessCursorShape(cursorPacket, shapeData);
            }
            // Force an immediate render so the cursor appears even on a static screen.
            // Use a non-blocking acquire (timeout=0): if the mutex is free we do a
            // lightweight release(key=1) + UpdateApplicationWindow cycle; if it's busy
            // the cursor will be drawn on the next regular frame render.
            if (!m_Occluded && m_KeyMutex)
            {
                HRESULT hr = m_KeyMutex->AcquireSync(0, 0);
                if (SUCCEEDED(hr))
                {
                    m_KeyMutex->ReleaseSync(1);
                    m_OutMgr.UpdateApplicationWindow(&m_PtrInfo, &m_Occluded);
                    LOG_DEBUG("ClientLogic::RunLoop: UpdateApplicationWindow");
                }
            }
            continue;
        }

        if (pktHeader.Type != PACKET_TYPE_FRAME)
        {
            LOG_WARN("ClientLogic::RunLoop: unknown packet type {}, disconnecting", pktHeader.Type);
            break; // Unknown packet type
        }

        std::vector<BYTE> uncompressedData;
        FramePacketHeader header;
        if (!m_NetClient.ReceiveFramePacketBody(pktHeader, uncompressedData, header))
        {
            LOG_WARN("ClientLogic::RunLoop: ReceiveFramePacketBody failed (connection lost)");
            break; // Connection closed or error
        }

        LOG_TRACE("ClientLogic::RunLoop: FRAME dirty={} move={} delta={}",
                  header.DirtyRectCount, header.MoveRectCount, header.IsDeltaEncoded ? 1 : 0);

        FRAME_DATA frameData;
        RtlZeroMemory(&frameData, sizeof(frameData));
        frameData.DirtyCount = header.DirtyRectCount;
        frameData.MoveCount = header.MoveRectCount;
        frameData.Frame = m_LocalTexture;
        frameData.FrameInfo.TotalMetadataBufferSize = header.MoveRectCount * sizeof(DXGI_OUTDUPL_MOVE_RECT) + header.DirtyRectCount * sizeof(RECT);

            size_t offset = sizeof(FramePacketHeader);
            
            RECT* dirtyRects = nullptr;
            if (header.DirtyRectCount > 0)
            {
                dirtyRects = reinterpret_cast<RECT*>(uncompressedData.data() + offset);
                offset += header.DirtyRectCount * sizeof(RECT);
            }

            DXGI_OUTDUPL_MOVE_RECT* moveRects = nullptr;
            if (header.MoveRectCount > 0)
            {
                moveRects = reinterpret_cast<DXGI_OUTDUPL_MOVE_RECT*>(uncompressedData.data() + offset);
                offset += header.MoveRectCount * sizeof(DXGI_OUTDUPL_MOVE_RECT);
            }

            //if (header.HasPointerInfo)
            //{
            //    PTR_INFO* ptrInfo = reinterpret_cast<PTR_INFO*>(uncompressedData.data() + offset);
            //    offset += sizeof(PTR_INFO);

            //    m_PtrInfo.Position = ptrInfo->Position;
            //    m_PtrInfo.Visible = ptrInfo->Visible;
            //    m_PtrInfo.WhoUpdatedPositionLast = ptrInfo->WhoUpdatedPositionLast;
            //    m_PtrInfo.LastTimeStamp = ptrInfo->LastTimeStamp;

            //    if (ptrInfo->BufferSize > 0)
            //    {
            //        if (m_PtrInfo.BufferSize < ptrInfo->BufferSize)
            //        {
            //            if (m_PtrInfo.PtrShapeBuffer) delete[] m_PtrInfo.PtrShapeBuffer;
            //            m_PtrInfo.PtrShapeBuffer = new BYTE[ptrInfo->BufferSize];
            //            m_PtrInfo.BufferSize = ptrInfo->BufferSize;
            //        }
            //        m_PtrInfo.ShapeInfo = ptrInfo->ShapeInfo;
            //        memcpy(m_PtrInfo.PtrShapeBuffer, uncompressedData.data() + offset, ptrInfo->BufferSize);
            //        offset += ptrInfo->BufferSize;
            //    }
            //}

            std::vector<BYTE> pixelData;
            if (header.DirtyRectCount > 0 && offset < uncompressedData.size())
            {
                pixelData.assign(uncompressedData.begin() + offset, uncompressedData.end());

                // Expand BGR → BGRA (Alpha = 0xFF) if Alpha was stripped by the sender
                if (header.PixelFormatFlags & 0x01)
                {
                    size_t totalPixels = 0;
                    for (UINT ri = 0; ri < header.DirtyRectCount; ++ri)
                    {
                        UINT w = dirtyRects[ri].right  - dirtyRects[ri].left;
                        UINT h = dirtyRects[ri].bottom - dirtyRects[ri].top;
                        totalPixels += w * h;
                    }
                    std::vector<BYTE> bgraData(totalPixels * 4);
                    size_t bgrOff = 0, bgraOff = 0;
                    while (bgrOff + 3 <= pixelData.size())
                    {
                        bgraData[bgraOff]     = pixelData[bgrOff];
                        bgraData[bgraOff + 1] = pixelData[bgrOff + 1];
                        bgraData[bgraOff + 2] = pixelData[bgrOff + 2];
                        bgraData[bgraOff + 3] = 0xFF;
                        bgrOff  += 3;
                        bgraOff += 4;
                    }
                    pixelData = std::move(bgraData);
                }

                // Ensure previous-frame buffer is sized for the full screen
                size_t fullFrameSize = static_cast<size_t>(m_InitData.Width) * m_InitData.Height * 4;
                if (m_PrevFrame.size() != fullFrameSize)
                {
                    m_PrevFrame.assign(fullFrameSize, 0);
                }

                if (header.IsDeltaEncoded)
                {
                    // XOR-decode each dirty rect's pixel data in-place using SSE2
                    size_t pixelOffset = 0;
                    for (UINT ri = 0; ri < header.DirtyRectCount; ++ri)
                    {
                        UINT width  = dirtyRects[ri].right  - dirtyRects[ri].left;
                        UINT height = dirtyRects[ri].bottom - dirtyRects[ri].top;
                        UINT rowBytes = width * 4;

                        for (UINT y = 0; y < height; ++y)
                        {
                            BYTE* dstRow  = pixelData.data() + pixelOffset;
                            BYTE* prevRow = m_PrevFrame.data()
                                            + ((dirtyRects[ri].top + y) * m_InitData.Width + dirtyRects[ri].left) * 4;

                            UINT x = 0;
                            for (; x + 16 <= rowBytes; x += 16)
                            {
                                __m128i delta = _mm_loadu_si128(reinterpret_cast<const __m128i*>(dstRow  + x));
                                __m128i prev  = _mm_loadu_si128(reinterpret_cast<const __m128i*>(prevRow + x));
                                __m128i cur   = _mm_xor_si128(delta, prev);
                                _mm_storeu_si128(reinterpret_cast<__m128i*>(dstRow  + x), cur);
                                _mm_storeu_si128(reinterpret_cast<__m128i*>(prevRow + x), cur);
                            }
                            for (; x < rowBytes; ++x)
                            {
                                dstRow[x]  = dstRow[x] ^ prevRow[x];
                                prevRow[x] = dstRow[x];
                            }

                            pixelOffset += rowBytes;
                        }
                    }
                }
                else
                {
                    // Full frame: update previous-frame buffer from raw pixel data
                    size_t pixelOffset = 0;
                    for (UINT ri = 0; ri < header.DirtyRectCount; ++ri)
                    {
                        UINT width  = dirtyRects[ri].right  - dirtyRects[ri].left;
                        UINT height = dirtyRects[ri].bottom - dirtyRects[ri].top;
                        UINT rowBytes = width * 4;
                        for (UINT y = 0; y < height; ++y)
                        {
                            BYTE* prevRow = m_PrevFrame.data()
                                            + ((dirtyRects[ri].top + y) * m_InitData.Width + dirtyRects[ri].left) * 4;
                            memcpy(prevRow, pixelData.data() + pixelOffset, rowBytes);
                            pixelOffset += rowBytes;
                        }
                    }
                }

                UpdateLocalTexture(pixelData, dirtyRects, header.DirtyRectCount);
            }

            // Construct MetaData buffer for DisplayManager
            std::vector<BYTE> metaDataBuffer;
            if (header.MoveRectCount > 0)
            {
                metaDataBuffer.insert(metaDataBuffer.end(), reinterpret_cast<BYTE*>(moveRects), reinterpret_cast<BYTE*>(moveRects) + header.MoveRectCount * sizeof(DXGI_OUTDUPL_MOVE_RECT));
            }
            if (header.DirtyRectCount > 0)
            {
                metaDataBuffer.insert(metaDataBuffer.end(), reinterpret_cast<BYTE*>(dirtyRects), reinterpret_cast<BYTE*>(dirtyRects) + header.DirtyRectCount * sizeof(RECT));
            }
            frameData.MetaData = metaDataBuffer.empty() ? nullptr : metaDataBuffer.data();

            HRESULT hr = m_KeyMutex->AcquireSync(0, 1000);
            if (SUCCEEDED(hr) && hr != static_cast<HRESULT>(WAIT_TIMEOUT))
            {
                DXGI_OUTPUT_DESC deskDesc;
                RtlZeroMemory(&deskDesc, sizeof(deskDesc));
                deskDesc.DesktopCoordinates.left = 0;
                deskDesc.DesktopCoordinates.top = 0;
                deskDesc.DesktopCoordinates.right = m_InitData.Width;
                deskDesc.DesktopCoordinates.bottom = m_InitData.Height;

                m_DispMgr.ProcessFrame(&frameData, m_SharedSurf, 0, 0, &deskDesc);
                m_KeyMutex->ReleaseSync(1);
            }

            if (!m_Occluded)
            {
                m_OutMgr.UpdateApplicationWindow(&m_PtrInfo, &m_Occluded);
            }

            // Update FPS in window title every second
            m_FrameCount++;
            DWORD now = GetTickCount();
            DWORD elapsed = now - m_LastFPSTick;
            if (elapsed >= 1000)
            {
                float fps = static_cast<float>(m_FrameCount) * 1000.0f / static_cast<float>(elapsed);
                LOG_DEBUG("ClientLogic::RunLoop: render FPS={:.1f}", fps);
                UpdateWindowTitle(fps);
                m_FrameCount = 0;
                m_LastFPSTick = now;
            }
    }
    LOG_INFO("ClientLogic::RunLoop exiting");
}
