// THIS CODE AND INFORMATION IS PROVIDED "AS IS" WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING BUT NOT LIMITED TO
// THE IMPLIED WARRANTIES OF MERCHANTABILITY AND/OR FITNESS FOR A
// PARTICULAR PURPOSE.
//
// Copyright (c) Microsoft Corporation. All rights reserved

#include "NetworkManager.h"
#include <emmintrin.h>
#include <cstring>
#include <iostream>

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
    if (!m_Compressor.Initialize(compressLevel))
    {
        return false;
    }
    return m_Server.Initialize(port);
}

bool NetworkManager::WaitForClient()
{
    return m_Server.WaitForClient();
}

bool NetworkManager::SendInitPacket(UINT32 width, UINT32 height, DXGI_FORMAT format)
{
    m_ScreenWidth = width;
    m_ScreenHeight = height;
    m_NeedsFullFrame = true;
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

    if (!m_Server.SendData(&header, sizeof(header))) return false;
    return m_Server.SendData(compressedData.data(), compressedData.size());
}

bool NetworkManager::SendFramePacket(const FRAME_DATA* data, const PTR_INFO* ptrInfo, ID3D11Device* device, ID3D11DeviceContext* context)
{
    if (!data || !data->Frame) return false;

    std::vector<BYTE> uncompressedPayload;

    FramePacketHeader frameHeader;
    frameHeader.DirtyRectCount = data->DirtyCount;
    frameHeader.MoveRectCount = data->MoveCount;
    frameHeader.HasPointerInfo = (ptrInfo && ptrInfo->Visible);
    frameHeader.IsDeltaEncoded = !m_NeedsFullFrame;

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
        if (ReadPixelsFromGPU(data->Frame, device, context, dirtyRects, dirtyCount, pixelData))
        {
            uncompressedPayload.insert(uncompressedPayload.end(), pixelData.begin(), pixelData.end());
        }
    }

    // Compress Payload
    std::vector<BYTE> compressedData;
    if (!m_Compressor.Compress(uncompressedPayload.data(), uncompressedPayload.size(), compressedData))
    {
        return false;
    }

    printf("uncompressedPayload Size %u, compressedData %u\n", uncompressedPayload.size(), compressedData.size());

    // Send Header
    PacketHeader header;
    header.MagicNumber = PACKET_MAGIC_NUMBER;
    header.Type = PACKET_TYPE_FRAME;
    header.CompressedSize = static_cast<UINT32>(compressedData.size());
    header.UncompressedSize = static_cast<UINT32>(uncompressedPayload.size());

    if (!m_Server.SendData(&header, sizeof(header))) return false;

    // Send Payload
    if (!m_Server.SendData(compressedData.data(), compressedData.size())) return false;

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

    // Calculate total size needed for pixels
    size_t totalPixelSize = 0;
    UINT bytesPerPixel = 4; // Assuming DXGI_FORMAT_B8G8R8A8_UNORM
    for (UINT i = 0; i < dirtyCount; ++i)
    {
        UINT width = dirtyRects[i].right - dirtyRects[i].left;
        UINT height = dirtyRects[i].bottom - dirtyRects[i].top;
        totalPixelSize += width * height * bytesPerPixel;
    }

    // Ensure previous-frame buffer is sized for the full screen
    size_t fullFrameSize = static_cast<size_t>(m_ScreenWidth) * m_ScreenHeight * 4;
    if (m_PrevFrame.size() != fullFrameSize)
    {
        m_PrevFrame.assign(fullFrameSize, 0);
    }

    outPixels.resize(totalPixelSize);
    size_t outOffset = 0;

    // Copy dirty pixels with SSE2 XOR delta encoding
    for (UINT i = 0; i < dirtyCount; ++i)
    {
        UINT width = dirtyRects[i].right - dirtyRects[i].left;
        UINT height = dirtyRects[i].bottom - dirtyRects[i].top;
        UINT rowBytes = width * bytesPerPixel;

        for (UINT y = 0; y < height; ++y)
        {
            const BYTE* srcRow = static_cast<const BYTE*>(mapped.pData)
                                 + ((dirtyRects[i].top + y) * mapped.RowPitch)
                                 + (dirtyRects[i].left * bytesPerPixel);
            BYTE* prevRow = m_PrevFrame.data()
                            + ((dirtyRects[i].top + y) * m_ScreenWidth + dirtyRects[i].left) * bytesPerPixel;
            BYTE* dstRow = outPixels.data() + outOffset;

            UINT x = 0;
            // Process 16 bytes at a time using SSE2
            for (; x + 16 <= rowBytes; x += 16)
            {
                __m128i cur  = _mm_loadu_si128(reinterpret_cast<const __m128i*>(srcRow  + x));
                __m128i prev = _mm_loadu_si128(reinterpret_cast<const __m128i*>(prevRow + x));
                _mm_storeu_si128(reinterpret_cast<__m128i*>(dstRow + x), _mm_xor_si128(cur, prev));
                _mm_storeu_si128(reinterpret_cast<__m128i*>(prevRow + x), cur);
            }
            // Handle remaining bytes
            for (; x < rowBytes; ++x)
            {
                dstRow[x] = srcRow[x] ^ prevRow[x];
                prevRow[x] = srcRow[x];
            }

            outOffset += rowBytes;
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
    m_Server.Disconnect();
}
