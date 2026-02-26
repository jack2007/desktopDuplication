// THIS CODE AND INFORMATION IS PROVIDED "AS IS" WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING BUT NOT LIMITED TO
// THE IMPLIED WARRANTIES OF MERCHANTABILITY AND/OR FITNESS FOR A
// PARTICULAR PURPOSE.
//
// Copyright (c) Microsoft Corporation. All rights reserved

#include "NetworkManager.h"
#include <emmintrin.h>
#include <tmmintrin.h>
#include <cstring>
#include <iostream>
#include "Logger.h"

NetworkManager::NetworkManager() : m_StagingTexture(nullptr), m_NeedsFullFrame(true), m_ScreenWidth(0), m_ScreenHeight(0)
{
    RtlZeroMemory(&m_StagingDesc, sizeof(m_StagingDesc));
}

NetworkManager::~NetworkManager()
{
    Disconnect();
    if (m_StagingTexture)
    {
        m_StagingTexture->Release();
        m_StagingTexture = nullptr;
    }
}

bool NetworkManager::Initialize(int port, unsigned int compressLevel)
{
    LOG_INFO("NetworkManager::Initialize port={}, compressLevel={}", port, compressLevel);
    if (!m_Compressor.Initialize(compressLevel))
    {
        LOG_ERROR("NetworkManager::Initialize: ZstdCompressor::Initialize failed");
        return false;
    }
    return m_Server.Initialize(port);
}

bool NetworkManager::WaitForClient()
{
    return m_Server.WaitForClient();
}

bool NetworkManager::SendInitPacket(UINT32 width, UINT32 height, DXGI_FORMAT format, INT32 desktopLeft, INT32 desktopTop)
{
    LOG_INFO("NetworkManager::SendInitPacket width={}, height={}, format={}, left={}, top={}",
             width, height, static_cast<int>(format), desktopLeft, desktopTop);
    m_ScreenWidth = width;
    m_ScreenHeight = height;
    m_NeedsFullFrame = true;
    m_MouseController.SetCaptureArea(desktopLeft, desktopTop, width, height);
    // Reconnect can recreate D3D device/context on duplication thread.
    // Drop old staging texture so ReadPixelsFromGPU rebuilds it on the current device.
    if (m_StagingTexture)
    {
        m_StagingTexture->Release();
        m_StagingTexture = nullptr;
        RtlZeroMemory(&m_StagingDesc, sizeof(m_StagingDesc));
    }
    // Reset previous frame buffer so the first frame after (re)connect is sent as raw pixels
    m_PrevFrame.assign(static_cast<size_t>(width) * height * 4, 0);

    InitPacket initData;
    initData.Width = width;
    initData.Height = height;
    initData.Format = format;

    std::vector<BYTE> compressedData;
    if (!m_Compressor.Compress(&initData, sizeof(initData), compressedData))
    {
        return false;
    }

    PacketHeader header;
    header.MagicNumber = PACKET_MAGIC_NUMBER;
    header.Type = PACKET_TYPE_INIT;
    header.CompressedSize = static_cast<UINT32>(compressedData.size());
    header.UncompressedSize = sizeof(initData);
    header.ProtocolVersion = 1;
    header.Reserved[0] = 0;
    header.Reserved[1] = 0;
    header.Reserved[2] = 0;

    if (!m_Server.SendData(&header, sizeof(header))) { LOG_ERROR("NetworkManager::SendInitPacket: SendData(header) failed"); return false; }
    if (!m_Server.SendData(compressedData.data(), compressedData.size())) { LOG_ERROR("NetworkManager::SendInitPacket: SendData(payload) failed"); return false; }

    // Proactively send the current cursor shape so the client sees a cursor immediately
    CursorShapePacket cursorPacket;
    std::vector<BYTE> shapeData;
    if (m_MouseController.GetCurrentCursorShape(cursorPacket, shapeData))
    {
        if (!SendCursorShape(cursorPacket, shapeData))
        {
            LOG_WARN("NetworkManager::SendInitPacket: SendCursorShape failed");
        }
    }

    return true;
}

bool NetworkManager::SendFramePacket(const FRAME_DATA* data, const PTR_INFO* ptrInfo, ID3D11Device* device, ID3D11DeviceContext* context)
{
    if (!data || !data->Frame) return false;

    // Drain queued input before heavy frame work to improve interactivity.
    ProcessPendingMouseInput();

    std::vector<BYTE> uncompressedPayload;

    FramePacketHeader frameHeader;
    frameHeader.DirtyRectCount = data->DirtyCount;
    frameHeader.MoveRectCount = data->MoveCount;
    frameHeader.HasPointerInfo = (ptrInfo && ptrInfo->Visible);
    frameHeader.IsDeltaEncoded = !m_NeedsFullFrame;
    frameHeader.PixelFormatFlags = 0x01; // Alpha stripped, pixel data is BGR 3 bytes/pixel
    frameHeader.Reserved = 0;

    RECT fullScreenRect = { 0, 0, static_cast<LONG>(m_ScreenWidth), static_cast<LONG>(m_ScreenHeight) };
    RECT* dirtyRects = nullptr;
    UINT dirtyCount = 0;

    if (m_NeedsFullFrame)
    {
        frameHeader.DirtyRectCount = 1;
        frameHeader.MoveRectCount = 0;
        dirtyRects = &fullScreenRect;
        dirtyCount = 1;
    }
    else
    {
        if (data->DirtyCount > 0)
        {
            dirtyRects = reinterpret_cast<RECT*>(data->MetaData + (data->MoveCount * sizeof(DXGI_OUTDUPL_MOVE_RECT)));
            dirtyCount = data->DirtyCount;
        }
    }

    // Append FrameHeader
    uncompressedPayload.insert(uncompressedPayload.end(), reinterpret_cast<BYTE*>(&frameHeader), reinterpret_cast<BYTE*>(&frameHeader) + sizeof(frameHeader));

    // Append Dirty Rects
    if (dirtyCount > 0)
    {
        uncompressedPayload.insert(uncompressedPayload.end(), reinterpret_cast<BYTE*>(dirtyRects), reinterpret_cast<BYTE*>(dirtyRects) + (dirtyCount * sizeof(RECT)));
    }

    // Append Move Rects
    if (!m_NeedsFullFrame && data->MoveCount > 0)
    {
        DXGI_OUTDUPL_MOVE_RECT* moveRects = reinterpret_cast<DXGI_OUTDUPL_MOVE_RECT*>(data->MetaData);
        uncompressedPayload.insert(uncompressedPayload.end(), reinterpret_cast<BYTE*>(moveRects), reinterpret_cast<BYTE*>(moveRects) + (data->MoveCount * sizeof(DXGI_OUTDUPL_MOVE_RECT)));
    }

    // Append Pointer Info
    if (frameHeader.HasPointerInfo)
    {
        uncompressedPayload.insert(uncompressedPayload.end(), reinterpret_cast<const BYTE*>(ptrInfo), reinterpret_cast<const BYTE*>(ptrInfo) + sizeof(PTR_INFO));
        if (ptrInfo->BufferSize > 0 && ptrInfo->PtrShapeBuffer)
        {
            uncompressedPayload.insert(uncompressedPayload.end(), ptrInfo->PtrShapeBuffer, ptrInfo->PtrShapeBuffer + ptrInfo->BufferSize);
        }
    }

    // Append Pixel Data (Only for dirty rects)
    if (dirtyCount > 0)
    {
        std::vector<BYTE> pixelData;
        if (!ReadPixelsFromGPU(data->Frame, device, context, dirtyRects, dirtyCount, pixelData))
        {
            LOG_ERROR("NetworkManager::SendFramePacket: ReadPixelsFromGPU failed (dirtyCount={})", dirtyCount);
            return false;
        }
        uncompressedPayload.insert(uncompressedPayload.end(), pixelData.begin(), pixelData.end());
    }

    // Compress Payload
    ProcessPendingMouseInput();
    std::vector<BYTE> compressedData;
    if (!m_Compressor.Compress(uncompressedPayload.data(), uncompressedPayload.size(), compressedData))
    {
        return false;
    }

    //printf("uncompressedPayload Size %u, compressedData %u\n", uncompressedPayload.size(), compressedData.size());

    LOG_DEBUG("NetworkManager::SendFramePacket uncompressedSize={}, compressedSize={}, dirtyRects={}, moveRects={}",
              uncompressedPayload.size(), compressedData.size(),
              frameHeader.DirtyRectCount, frameHeader.MoveRectCount);

    // Send Header
    PacketHeader header;
    header.MagicNumber = PACKET_MAGIC_NUMBER;
    header.Type = PACKET_TYPE_FRAME;
    header.CompressedSize = static_cast<UINT32>(compressedData.size());
    header.UncompressedSize = static_cast<UINT32>(uncompressedPayload.size());
    header.ProtocolVersion = 1;
    header.Reserved[0] = 0;
    header.Reserved[1] = 0;
    header.Reserved[2] = 0;

    if (!m_Server.SendData(&header, sizeof(header))) { LOG_ERROR("NetworkManager::SendFramePacket: SendData(header) failed"); return false; }

    // Send Payload
    if (!m_Server.SendData(compressedData.data(), compressedData.size())) { LOG_ERROR("NetworkManager::SendFramePacket: SendData(payload) failed"); return false; }

    m_NeedsFullFrame = false;
    return true;
}

bool NetworkManager::ReadPixelsFromGPU(ID3D11Texture2D* srcTexture, ID3D11Device* device, ID3D11DeviceContext* context, const RECT* dirtyRects, UINT dirtyCount, std::vector<BYTE>& outPixels)
{
    D3D11_TEXTURE2D_DESC srcDesc;
    srcTexture->GetDesc(&srcDesc);

    // Create or resize staging texture if needed
    if (!m_StagingTexture || m_StagingDesc.Width != srcDesc.Width || m_StagingDesc.Height != srcDesc.Height || m_StagingDesc.Format != srcDesc.Format)
    {
        if (m_StagingTexture)
        {
            m_StagingTexture->Release();
            m_StagingTexture = nullptr;
        }

        m_StagingDesc = srcDesc;
        m_StagingDesc.Usage = D3D11_USAGE_STAGING;
        m_StagingDesc.BindFlags = 0;
        m_StagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        m_StagingDesc.MiscFlags = 0;
        m_StagingDesc.MipLevels = 1;
        m_StagingDesc.ArraySize = 1;

        HRESULT hr = device->CreateTexture2D(&m_StagingDesc, nullptr, &m_StagingTexture);
        if (FAILED(hr)) return false;
    }

    if (dirtyCount == 0) return true;

    // Copy only dirty regions from GPU to Staging
    for (UINT i = 0; i < dirtyCount; ++i)
    {
        D3D11_BOX box;
        box.left   = dirtyRects[i].left;
        box.right  = dirtyRects[i].right;
        box.top    = dirtyRects[i].top;
        box.bottom = dirtyRects[i].bottom;
        box.front  = 0;
        box.back   = 1;
        context->CopySubresourceRegion(m_StagingTexture, 0, box.left, box.top, 0, srcTexture, 0, &box);
    }

    // Map Staging Texture
    D3D11_MAPPED_SUBRESOURCE mapped;
    HRESULT hr = context->Map(m_StagingTexture, 0, D3D11_MAP_READ, 0, &mapped);
    if (FAILED(hr)) return false;

    // Calculate total size needed for pixels (BGR: 3 bytes/pixel)
    size_t totalPixelSize = 0;
    UINT bytesPerPixel = 4; // Assuming DXGI_FORMAT_B8G8R8A8_UNORM (internal BGRA)
    for (UINT i = 0; i < dirtyCount; ++i)
    {
        UINT width = dirtyRects[i].right - dirtyRects[i].left;
        UINT height = dirtyRects[i].bottom - dirtyRects[i].top;
        totalPixelSize += width * height * 3; // BGR output: 3 bytes/pixel
    }

    // Ensure previous-frame buffer is sized for the full screen
    size_t fullFrameSize = static_cast<size_t>(m_ScreenWidth) * m_ScreenHeight * 4;
    if (m_PrevFrame.size() != fullFrameSize)
    {
        m_PrevFrame.assign(fullFrameSize, 0);
    }

    outPixels.resize(totalPixelSize);
    size_t outOffset = 0;

    // SSSE3 shuffle mask: extract BGR bytes from 4 packed BGRA pixels (16 bytes → 12 bytes)
    // Input layout:  B0 G0 R0 A0  B1 G1 R1 A1  B2 G2 R2 A2  B3 G3 R3 A3
    // Output layout: B0 G0 R0     B1 G1 R1     B2 G2 R2     B3 G3 R3  (+ 4 don't-care bytes)
    static const __m128i bgra_to_bgr_mask = _mm_set_epi8(
        (char)0x80, (char)0x80, (char)0x80, (char)0x80,  // bytes 15-12: zeroed (don't care)
        14, 13, 12,   // B3 G3 R3
        10,  9,  8,   // B2 G2 R2
         6,  5,  4,   // B1 G1 R1
         2,  1,  0    // B0 G0 R0
    );

    // Copy dirty pixels: SSE2 XOR delta on BGRA, then SSSE3 Alpha strip to BGR output
    for (UINT i = 0; i < dirtyCount; ++i)
    {
        UINT width = dirtyRects[i].right - dirtyRects[i].left;
        UINT height = dirtyRects[i].bottom - dirtyRects[i].top;
        UINT rowBytes = width * bytesPerPixel; // BGRA row width in bytes

        for (UINT y = 0; y < height; ++y)
        {
            const BYTE* srcRow = static_cast<const BYTE*>(mapped.pData)
                                 + ((dirtyRects[i].top + y) * mapped.RowPitch)
                                 + (dirtyRects[i].left * bytesPerPixel);
            BYTE* prevRow = m_PrevFrame.data()
                            + ((dirtyRects[i].top + y) * m_ScreenWidth + dirtyRects[i].left) * bytesPerPixel;
            BYTE* dstRow = outPixels.data() + outOffset;

            UINT x = 0;    // BGRA source byte index
            UINT bgrX = 0; // BGR destination byte index

            // Process 4 BGRA pixels (16 bytes) → 12 BGR bytes at a time using SSSE3
            for (; x + 16 <= rowBytes; x += 16, bgrX += 12)
            {
                __m128i cur   = _mm_loadu_si128(reinterpret_cast<const __m128i*>(srcRow  + x));
                __m128i prev  = _mm_loadu_si128(reinterpret_cast<const __m128i*>(prevRow + x));
                __m128i xored = _mm_xor_si128(cur, prev);
                _mm_storeu_si128(reinterpret_cast<__m128i*>(prevRow + x), cur);
                // Strip Alpha: shuffle XOR result (BGRA) → BGR (12 bytes + 4 zeroed)
                __m128i bgr = _mm_shuffle_epi8(xored, bgra_to_bgr_mask);
                memcpy(dstRow + bgrX, &bgr, 12);
            }
            // Scalar fallback for remaining pixels (< 4 pixels)
            for (; x < rowBytes; x += 4, bgrX += 3)
            {
                dstRow[bgrX]     = srcRow[x]     ^ prevRow[x];
                dstRow[bgrX + 1] = srcRow[x + 1] ^ prevRow[x + 1];
                dstRow[bgrX + 2] = srcRow[x + 2] ^ prevRow[x + 2];
                prevRow[x]     = srcRow[x];
                prevRow[x + 1] = srcRow[x + 1];
                prevRow[x + 2] = srcRow[x + 2];
                prevRow[x + 3] = srcRow[x + 3];
            }

            outOffset += width * 3; // BGR: 3 bytes/pixel
        }
    }

    context->Unmap(m_StagingTexture, 0);
    return true;
}

bool NetworkManager::IsConnected()
{
    return m_Server.IsConnected();
}

void NetworkManager::Disconnect()
{
    LOG_INFO("NetworkManager::Disconnect");
    m_Server.Disconnect();
}

bool NetworkManager::HasClientData()
{
    return m_Server.HasData();
}

bool NetworkManager::ReceiveMouseInput(MouseInputPacket& outInput)
{
    PacketHeader header;
    if (!m_Server.ReceiveData(&header, sizeof(header))) return false;
    if (header.MagicNumber != PACKET_MAGIC_NUMBER || header.Type != PACKET_TYPE_MOUSE_INPUT) return false;
    if (header.UncompressedSize != sizeof(MouseInputPacket)) return false;
    return m_Server.ReceiveData(&outInput, sizeof(MouseInputPacket));
}

bool NetworkManager::ReceiveKeyboardInput(KeyboardInputPacket& outInput)
{
    PacketHeader header;
    if (!m_Server.ReceiveData(&header, sizeof(header))) return false;
    if (header.MagicNumber != PACKET_MAGIC_NUMBER || header.Type != PACKET_TYPE_KEYBOARD_INPUT) return false;
    if (header.UncompressedSize != sizeof(KeyboardInputPacket)) return false;
    return m_Server.ReceiveData(&outInput, sizeof(KeyboardInputPacket));
}

bool NetworkManager::SendCursorShape(const CursorShapePacket& packet, const std::vector<BYTE>& shapeData)
{
    UINT32 payloadSize = static_cast<UINT32>(sizeof(CursorShapePacket)) + packet.ShapeBufferSize;

    PacketHeader header;
    header.MagicNumber      = PACKET_MAGIC_NUMBER;
    header.Type             = PACKET_TYPE_CURSOR_SHAPE;
    header.CompressedSize   = payloadSize;
    header.UncompressedSize = payloadSize;
    header.ProtocolVersion  = 1;
    header.Reserved[0]      = 0;
    header.Reserved[1]      = 0;
    header.Reserved[2]      = 0;

    if (!m_Server.SendData(&header, sizeof(header))) return false;
    if (!m_Server.SendData(&packet, sizeof(CursorShapePacket))) return false;
    if (packet.ShapeBufferSize > 0 && !shapeData.empty())
    {
        if (!m_Server.SendData(shapeData.data(), packet.ShapeBufferSize)) return false;
    }
    return true;
}

void NetworkManager::ProcessPendingMouseInput()
{
    int processedMouse = 0;
    int processedKeyboard = 0;
    MouseInputPacket lastInput = {};
    bool hasLastInput = false;
    MouseInputPacket pendingMoveInput = {};
    bool hasPendingMove = false;
    while (m_Server.HasData())
    {
        PacketHeader header = {};
        if (!m_Server.ReceiveData(&header, sizeof(header))) break;
        if (header.MagicNumber != PACKET_MAGIC_NUMBER)
        {
            break;
        }

        if (header.Type == PACKET_TYPE_MOUSE_INPUT)
        {
            if (header.UncompressedSize != sizeof(MouseInputPacket))
            {
                break;
            }

            MouseInputPacket input = {};
            if (!m_Server.ReceiveData(&input, sizeof(input)))
            {
                break;
            }

            // Coalesce burst mouse-move packets to the latest point so
            // click/wheel actions are not delayed by stale move backlog.
            if (input.InputType == static_cast<UINT8>(MOUSE_INPUT_MOVE))
            {
                pendingMoveInput = input;
                hasPendingMove = true;
                continue;
            }

            if (hasPendingMove)
            {
                m_MouseController.ProcessMouseInput(pendingMoveInput);
                lastInput = pendingMoveInput;
                hasLastInput = true;
                processedMouse++;
                hasPendingMove = false;
            }

            m_MouseController.ProcessMouseInput(input);
            lastInput = input;
            hasLastInput = true;
            processedMouse++;
            continue;
        }

        if (header.Type == PACKET_TYPE_KEYBOARD_INPUT)
        {
            if (header.UncompressedSize != sizeof(KeyboardInputPacket))
            {
                break;
            }

            KeyboardInputPacket input = {};
            if (!m_Server.ReceiveData(&input, sizeof(input)))
            {
                break;
            }

            // Flush pending mouse move before key event to preserve event ordering.
            if (hasPendingMove)
            {
                m_MouseController.ProcessMouseInput(pendingMoveInput);
                lastInput = pendingMoveInput;
                hasLastInput = true;
                processedMouse++;
                hasPendingMove = false;
            }

            m_KeyboardController.ProcessKeyboardInput(input);
            processedKeyboard++;
            continue;
        }

        // Unknown packet type from client.
        break;
    }

    if (hasPendingMove)
    {
        m_MouseController.ProcessMouseInput(pendingMoveInput);
        lastInput = pendingMoveInput;
        hasLastInput = true;
        processedMouse++;
    }

    if (processedMouse > 0 || processedKeyboard > 0)
    {
        if (hasLastInput)
        {
            LOG_DEBUG("NetworkManager::ProcessPendingMouseInput mouseProcessed={}, keyboardProcessed={}, lastType={}, x={}, y={}, wheel={}",
                      processedMouse,
                      processedKeyboard,
                      static_cast<unsigned>(lastInput.InputType),
                      lastInput.X, lastInput.Y, lastInput.WheelDelta);
        }
        if (processedMouse > 0)
        {
            CursorShapePacket cursorPacket;
            std::vector<BYTE> shapeData;
            if (m_MouseController.GetCurrentCursorShape(cursorPacket, shapeData))
            {
                if (!SendCursorShape(cursorPacket, shapeData))
                {
                    LOG_WARN("NetworkManager::ProcessPendingMouseInput: SendCursorShape failed");
                }
            }
        }
    }
}
