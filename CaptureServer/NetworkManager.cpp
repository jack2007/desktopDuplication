// THIS CODE AND INFORMATION IS PROVIDED "AS IS" WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING BUT NOT LIMITED TO
// THE IMPLIED WARRANTIES OF MERCHANTABILITY AND/OR FITNESS FOR A
// PARTICULAR PURPOSE.
//
// Copyright (c) Microsoft Corporation. All rights reserved

#include "NetworkManager.h"
#include <iostream>

// ---------------------------------------------------------------------------
// HLSL compute shader source – embedded so the binary is self-contained.
// The shader densely packs the pixels of each dirty rect into a flat linear
// buffer, eliminating the full-frame CopyResource PCIe transfer.
// ---------------------------------------------------------------------------
static const char* s_ShaderSource =
    "struct DirtyRectInfo\n"
    "{\n"
    "    int left;\n"
    "    int top;\n"
    "    int right;\n"
    "    int bottom;\n"
    "    int outputOffset;\n"
    "    int pad0;\n"
    "    int pad1;\n"
    "    int pad2;\n"
    "};\n"
    "\n"
    "Texture2D<float4>               InputTexture : register(t0);\n"
    "StructuredBuffer<DirtyRectInfo> DirtyRects   : register(t1);\n"
    "RWByteAddressBuffer             OutputBuffer : register(u0);\n"
    "\n"
    "cbuffer Constants : register(b0)\n"
    "{\n"
    "    uint DirtyRectCount;\n"
    "    uint pad0;\n"
    "    uint pad1;\n"
    "    uint pad2;\n"
    "};\n"
    "\n"
    "[numthreads(8, 8, 1)]\n"
    "void CSMain(uint3 GroupID       : SV_GroupID,\n"
    "            uint3 GroupThreadID : SV_GroupThreadID)\n"
    "{\n"
    "    uint rectIdx = GroupID.z;\n"
    "    if (rectIdx >= DirtyRectCount) return;\n"
    "\n"
    "    DirtyRectInfo rect = DirtyRects[rectIdx];\n"
    "    uint localX = GroupID.x * 8u + GroupThreadID.x;\n"
    "    uint localY = GroupID.y * 8u + GroupThreadID.y;\n"
    "    uint rectW  = (uint)(rect.right  - rect.left);\n"
    "    uint rectH  = (uint)(rect.bottom - rect.top);\n"
    "\n"
    "    if (localX >= rectW || localY >= rectH) return;\n"
    "\n"
    "    int px = rect.left + (int)localX;\n"
    "    int py = rect.top  + (int)localY;\n"
    "\n"
    "    // DXGI_FORMAT_B8G8R8A8_UNORM: x=B, y=G, z=R, w=A\n"
    "    float4 c = InputTexture.Load(int3(px, py, 0));\n"
    "    uint b = (uint)(saturate(c.x) * 255.0f + 0.5f);\n"
    "    uint g = (uint)(saturate(c.y) * 255.0f + 0.5f);\n"
    "    uint r = (uint)(saturate(c.z) * 255.0f + 0.5f);\n"
    "    uint a = (uint)(saturate(c.w) * 255.0f + 0.5f);\n"
    "    uint packed = b | (g << 8u) | (r << 16u) | (a << 24u);\n"
    "\n"
    "    uint outPixelIdx = (uint)rect.outputOffset + localY * rectW + localX;\n"
    "    OutputBuffer.Store(outPixelIdx * 4u, packed);\n"
    "}\n";

// ---------------------------------------------------------------------------
// CPU-side mirror of the HLSL DirtyRectInfo struct.
// Must match the HLSL layout exactly (8 × INT = 32 bytes per element).
// ---------------------------------------------------------------------------
struct DirtyRectInfo
{
    INT left, top, right, bottom;
    INT outputOffset;
    INT pad[3];
};

// ---------------------------------------------------------------------------
// CPU-side mirror of the HLSL constant buffer (16 bytes, one uint + padding).
// ---------------------------------------------------------------------------
struct ComputeConstants
{
    UINT DirtyRectCount;
    UINT pad[3];
};

// ---------------------------------------------------------------------------
// Constructor / destructor
// ---------------------------------------------------------------------------

NetworkManager::NetworkManager()
    : m_NeedsFullFrame(true)
    , m_ScreenWidth(0)
    , m_ScreenHeight(0)
    , m_ComputeShader(nullptr)
    , m_DirtyRectBuffer(nullptr)
    , m_DirtyRectSRV(nullptr)
    , m_OutputBuffer(nullptr)
    , m_OutputUAV(nullptr)
    , m_OutputStagingBuffer(nullptr)
    , m_ConstantBuffer(nullptr)
    , m_OutputCapacityPixels(0)
    , m_DirtyRectCapacity(0)
{
}

NetworkManager::~NetworkManager()
{
    Disconnect();
    CleanupComputeResources();
}

// ---------------------------------------------------------------------------
// Public interface
// ---------------------------------------------------------------------------

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
    m_ScreenWidth  = width;
    m_ScreenHeight = height;
    m_NeedsFullFrame = true;

    InitPacket initData;
    initData.Width  = width;
    initData.Height = height;
    initData.Format = format;

    std::vector<BYTE> compressedData;
    if (!m_Compressor.Compress(&initData, sizeof(initData), compressedData))
    {
        return false;
    }

    PacketHeader header;
    header.MagicNumber       = PACKET_MAGIC_NUMBER;
    header.Type              = PACKET_TYPE_INIT;
    header.CompressedSize    = static_cast<UINT32>(compressedData.size());
    header.UncompressedSize  = sizeof(initData);

    if (!m_Server.SendData(&header, sizeof(header))) return false;
    return m_Server.SendData(compressedData.data(), compressedData.size());
}

bool NetworkManager::SendFramePacket(const FRAME_DATA* data, const PTR_INFO* ptrInfo, ID3D11Device* device, ID3D11DeviceContext* context)
{
    if (!data || !data->Frame) return false;

    std::vector<BYTE> uncompressedPayload;

    FramePacketHeader frameHeader;
    frameHeader.DirtyRectCount = data->DirtyCount;
    frameHeader.MoveRectCount  = data->MoveCount;
    frameHeader.HasPointerInfo = (ptrInfo && ptrInfo->Visible);
    frameHeader.IsDensePacked  = true; // pixel data is always densely packed

    RECT fullScreenRect = { 0, 0, static_cast<LONG>(m_ScreenWidth), static_cast<LONG>(m_ScreenHeight) };
    RECT* dirtyRects = nullptr;
    UINT  dirtyCount = 0;

    if (m_NeedsFullFrame)
    {
        frameHeader.DirtyRectCount = 1;
        frameHeader.MoveRectCount  = 0;
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
    uncompressedPayload.insert(uncompressedPayload.end(),
        reinterpret_cast<BYTE*>(&frameHeader),
        reinterpret_cast<BYTE*>(&frameHeader) + sizeof(frameHeader));

    // Append Dirty Rects
    if (dirtyCount > 0)
    {
        uncompressedPayload.insert(uncompressedPayload.end(),
            reinterpret_cast<BYTE*>(dirtyRects),
            reinterpret_cast<BYTE*>(dirtyRects) + (dirtyCount * sizeof(RECT)));
    }

    // Append Move Rects
    if (!m_NeedsFullFrame && data->MoveCount > 0)
    {
        DXGI_OUTDUPL_MOVE_RECT* moveRects = reinterpret_cast<DXGI_OUTDUPL_MOVE_RECT*>(data->MetaData);
        uncompressedPayload.insert(uncompressedPayload.end(),
            reinterpret_cast<BYTE*>(moveRects),
            reinterpret_cast<BYTE*>(moveRects) + (data->MoveCount * sizeof(DXGI_OUTDUPL_MOVE_RECT)));
    }

    // Append Pointer Info
    if (frameHeader.HasPointerInfo)
    {
        uncompressedPayload.insert(uncompressedPayload.end(),
            reinterpret_cast<const BYTE*>(ptrInfo),
            reinterpret_cast<const BYTE*>(ptrInfo) + sizeof(PTR_INFO));
        if (ptrInfo->BufferSize > 0 && ptrInfo->PtrShapeBuffer)
        {
            uncompressedPayload.insert(uncompressedPayload.end(),
                ptrInfo->PtrShapeBuffer,
                ptrInfo->PtrShapeBuffer + ptrInfo->BufferSize);
        }
    }

    // Append Pixel Data – only dirty rects, densely packed via compute shader
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

    printf("uncompressedPayload Size %u, compressedData %u\n",
           static_cast<UINT>(uncompressedPayload.size()),
           static_cast<UINT>(compressedData.size()));

    // Send Header
    PacketHeader header;
    header.MagicNumber      = PACKET_MAGIC_NUMBER;
    header.Type             = PACKET_TYPE_FRAME;
    header.CompressedSize   = static_cast<UINT32>(compressedData.size());
    header.UncompressedSize = static_cast<UINT32>(uncompressedPayload.size());

    if (!m_Server.SendData(&header, sizeof(header))) return false;
    if (!m_Server.SendData(compressedData.data(), compressedData.size())) return false;

    m_NeedsFullFrame = false;
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

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

void NetworkManager::CleanupComputeResources()
{
    if (m_ComputeShader)       { m_ComputeShader->Release();       m_ComputeShader       = nullptr; }
    if (m_DirtyRectSRV)        { m_DirtyRectSRV->Release();        m_DirtyRectSRV        = nullptr; }
    if (m_DirtyRectBuffer)     { m_DirtyRectBuffer->Release();     m_DirtyRectBuffer     = nullptr; }
    if (m_OutputUAV)           { m_OutputUAV->Release();           m_OutputUAV           = nullptr; }
    if (m_OutputBuffer)        { m_OutputBuffer->Release();        m_OutputBuffer        = nullptr; }
    if (m_OutputStagingBuffer) { m_OutputStagingBuffer->Release(); m_OutputStagingBuffer = nullptr; }
    if (m_ConstantBuffer)      { m_ConstantBuffer->Release();      m_ConstantBuffer      = nullptr; }
    m_OutputCapacityPixels = 0;
    m_DirtyRectCapacity    = 0;
}

bool NetworkManager::InitializeComputeShader(ID3D11Device* device)
{
    // Compile shader from embedded source
    ID3DBlob* shaderBlob = nullptr;
    ID3DBlob* errorBlob  = nullptr;

    HRESULT hr = D3DCompile(
        s_ShaderSource,
        strlen(s_ShaderSource),
        "RLECompress.hlsl",   // source name for error messages
        nullptr,              // no macros
        nullptr,              // no includes
        "CSMain",
        "cs_5_0",
        D3DCOMPILE_OPTIMIZATION_LEVEL3,
        0,
        &shaderBlob,
        &errorBlob);

    if (errorBlob)
    {
        fprintf(stderr, "Shader compile message: %s\n",
                static_cast<const char*>(errorBlob->GetBufferPointer()));
        errorBlob->Release();
    }

    if (FAILED(hr))
    {
        fprintf(stderr, "Failed to compile compute shader (hr=0x%08X)\n", hr);
        if (shaderBlob) shaderBlob->Release();
        return false;
    }

    hr = device->CreateComputeShader(
        shaderBlob->GetBufferPointer(),
        shaderBlob->GetBufferSize(),
        nullptr,
        &m_ComputeShader);

    shaderBlob->Release();

    if (FAILED(hr))
    {
        fprintf(stderr, "Failed to create compute shader (hr=0x%08X)\n", hr);
        return false;
    }

    // Create constant buffer (16 bytes)
    D3D11_BUFFER_DESC cbDesc = {};
    cbDesc.ByteWidth      = sizeof(ComputeConstants);
    cbDesc.Usage          = D3D11_USAGE_DEFAULT;
    cbDesc.BindFlags      = D3D11_BIND_CONSTANT_BUFFER;
    cbDesc.CPUAccessFlags = 0;

    hr = device->CreateBuffer(&cbDesc, nullptr, &m_ConstantBuffer);
    if (FAILED(hr))
    {
        fprintf(stderr, "Failed to create constant buffer (hr=0x%08X)\n", hr);
        return false;
    }

    return true;
}

// Ensure m_DirtyRectBuffer / m_DirtyRectSRV can hold at least `count` elements.
static bool EnsureDirtyRectBuffer(ID3D11Device* device,
                                   ID3D11Buffer*& buf,
                                   ID3D11ShaderResourceView*& srv,
                                   UINT& capacity,
                                   UINT count)
{
    if (count <= capacity) return true;

    // Release old resources
    if (srv) { srv->Release(); srv = nullptr; }
    if (buf) { buf->Release(); buf = nullptr; }

    // Allocate with growth headroom
    UINT newCap = count * 2;

    D3D11_BUFFER_DESC bd = {};
    bd.ByteWidth           = newCap * sizeof(DirtyRectInfo);
    bd.Usage               = D3D11_USAGE_DEFAULT;
    bd.BindFlags           = D3D11_BIND_SHADER_RESOURCE;
    bd.CPUAccessFlags      = 0;
    bd.MiscFlags           = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
    bd.StructureByteStride = sizeof(DirtyRectInfo);

    HRESULT hr = device->CreateBuffer(&bd, nullptr, &buf);
    if (FAILED(hr)) return false;

    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format              = DXGI_FORMAT_UNKNOWN;
    srvDesc.ViewDimension       = D3D11_SRV_DIMENSION_BUFFER;
    srvDesc.Buffer.FirstElement = 0;
    srvDesc.Buffer.NumElements  = newCap;

    hr = device->CreateShaderResourceView(buf, &srvDesc, &srv);
    if (FAILED(hr)) return false;

    capacity = newCap;
    return true;
}

// Ensure m_OutputBuffer / m_OutputUAV / m_OutputStagingBuffer can hold
// at least `pixels` pixels (4 bytes each).
static bool EnsureOutputBuffer(ID3D11Device* device,
                                ID3D11Buffer*& outBuf,
                                ID3D11UnorderedAccessView*& outUAV,
                                ID3D11Buffer*& stagBuf,
                                UINT& capacity,
                                UINT pixels)
{
    if (pixels <= capacity) return true;

    // Release old resources
    if (outUAV)  { outUAV->Release();  outUAV  = nullptr; }
    if (outBuf)  { outBuf->Release();  outBuf  = nullptr; }
    if (stagBuf) { stagBuf->Release(); stagBuf = nullptr; }

    UINT newCap     = pixels * 2;
    UINT byteWidth  = newCap * 4u;

    // GPU output buffer (RWByteAddressBuffer)
    D3D11_BUFFER_DESC bd = {};
    bd.ByteWidth      = byteWidth;
    bd.Usage          = D3D11_USAGE_DEFAULT;
    bd.BindFlags      = D3D11_BIND_UNORDERED_ACCESS;
    bd.CPUAccessFlags = 0;
    bd.MiscFlags      = D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS;

    HRESULT hr = device->CreateBuffer(&bd, nullptr, &outBuf);
    if (FAILED(hr)) return false;

    D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
    uavDesc.Format              = DXGI_FORMAT_R32_TYPELESS;
    uavDesc.ViewDimension       = D3D11_UAV_DIMENSION_BUFFER;
    uavDesc.Buffer.FirstElement = 0;
    uavDesc.Buffer.NumElements  = newCap; // one uint32 per pixel
    uavDesc.Buffer.Flags        = D3D11_BUFFER_UAV_FLAG_RAW;

    hr = device->CreateUnorderedAccessView(outBuf, &uavDesc, &outUAV);
    if (FAILED(hr)) return false;

    // Staging buffer for CPU readback
    D3D11_BUFFER_DESC stagDesc = {};
    stagDesc.ByteWidth      = byteWidth;
    stagDesc.Usage          = D3D11_USAGE_STAGING;
    stagDesc.BindFlags      = 0;
    stagDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    stagDesc.MiscFlags      = 0;

    hr = device->CreateBuffer(&stagDesc, nullptr, &stagBuf);
    if (FAILED(hr)) return false;

    capacity = newCap;
    return true;
}

bool NetworkManager::ReadPixelsFromGPU(
    ID3D11Texture2D*    srcTexture,
    ID3D11Device*       device,
    ID3D11DeviceContext* context,
    const RECT*         dirtyRects,
    UINT                dirtyCount,
    std::vector<BYTE>&  outPixels)
{
    // -----------------------------------------------------------------------
    // Lazily initialise the compute shader and constant buffer.
    // -----------------------------------------------------------------------
    if (!m_ComputeShader)
    {
        if (!InitializeComputeShader(device))
            return false;
    }

    // -----------------------------------------------------------------------
    // Build per-rect info (pixel offsets) and find dispatch dimensions.
    // -----------------------------------------------------------------------
    std::vector<DirtyRectInfo> rectInfos(dirtyCount);
    INT  totalPixels = 0;
    UINT maxRectW    = 0;
    UINT maxRectH    = 0;

    for (UINT i = 0; i < dirtyCount; ++i)
    {
        UINT w = static_cast<UINT>(dirtyRects[i].right  - dirtyRects[i].left);
        UINT h = static_cast<UINT>(dirtyRects[i].bottom - dirtyRects[i].top);

        rectInfos[i].left         = dirtyRects[i].left;
        rectInfos[i].top          = dirtyRects[i].top;
        rectInfos[i].right        = dirtyRects[i].right;
        rectInfos[i].bottom       = dirtyRects[i].bottom;
        rectInfos[i].outputOffset = totalPixels;
        rectInfos[i].pad[0]       = 0;
        rectInfos[i].pad[1]       = 0;
        rectInfos[i].pad[2]       = 0;

        totalPixels += static_cast<INT>(w * h);
        if (w > maxRectW) maxRectW = w;
        if (h > maxRectH) maxRectH = h;
    }

    if (totalPixels == 0)
    {
        outPixels.clear();
        return true;
    }

    // -----------------------------------------------------------------------
    // Ensure GPU buffers are large enough.
    // -----------------------------------------------------------------------
    if (!EnsureDirtyRectBuffer(device,
                                m_DirtyRectBuffer, m_DirtyRectSRV,
                                m_DirtyRectCapacity, dirtyCount))
        return false;

    if (!EnsureOutputBuffer(device,
                             m_OutputBuffer, m_OutputUAV,
                             m_OutputStagingBuffer,
                             m_OutputCapacityPixels,
                             static_cast<UINT>(totalPixels)))
        return false;

    // -----------------------------------------------------------------------
    // Update dirty-rect structured buffer.
    // -----------------------------------------------------------------------
    D3D11_BOX box = {};
    box.left   = 0;
    box.right  = dirtyCount * sizeof(DirtyRectInfo);
    box.top    = 0; box.bottom = 1;
    box.front  = 0; box.back   = 1;
    context->UpdateSubresource(m_DirtyRectBuffer, 0, &box,
                               rectInfos.data(), 0, 0);

    // -----------------------------------------------------------------------
    // Update constant buffer.
    // -----------------------------------------------------------------------
    ComputeConstants constants;
    constants.DirtyRectCount = dirtyCount;
    constants.pad[0] = constants.pad[1] = constants.pad[2] = 0;
    context->UpdateSubresource(m_ConstantBuffer, 0, nullptr, &constants, 0, 0);

    // -----------------------------------------------------------------------
    // Create a temporary SRV for the source texture (one per call; cheap).
    // -----------------------------------------------------------------------
    D3D11_TEXTURE2D_DESC srcDesc;
    srcTexture->GetDesc(&srcDesc);

    D3D11_SHADER_RESOURCE_VIEW_DESC texSrvDesc = {};
    texSrvDesc.Format                    = srcDesc.Format;
    texSrvDesc.ViewDimension             = D3D11_SRV_DIMENSION_TEXTURE2D;
    texSrvDesc.Texture2D.MostDetailedMip = 0;
    texSrvDesc.Texture2D.MipLevels       = 1;

    ID3D11ShaderResourceView* texSRV = nullptr;
    HRESULT hr = device->CreateShaderResourceView(srcTexture, &texSrvDesc, &texSRV);
    if (FAILED(hr)) return false;

    // -----------------------------------------------------------------------
    // Dispatch the compute shader.
    // -----------------------------------------------------------------------
    UINT groupX = (maxRectW + 7u) / 8u;
    UINT groupY = (maxRectH + 7u) / 8u;
    UINT groupZ = dirtyCount;

    context->CSSetShader(m_ComputeShader, nullptr, 0);
    context->CSSetShaderResources(0, 1, &texSRV);
    context->CSSetShaderResources(1, 1, &m_DirtyRectSRV);
    context->CSSetUnorderedAccessViews(0, 1, &m_OutputUAV, nullptr);
    context->CSSetConstantBuffers(0, 1, &m_ConstantBuffer);

    context->Dispatch(groupX, groupY, groupZ);

    // Unbind resources
    texSRV->Release();
    ID3D11ShaderResourceView* nullSRV[2] = { nullptr, nullptr };
    ID3D11UnorderedAccessView* nullUAV   = nullptr;
    context->CSSetShaderResources(0, 2, nullSRV);
    context->CSSetUnorderedAccessViews(0, 1, &nullUAV, nullptr);
    context->CSSetShader(nullptr, nullptr, 0);

    // -----------------------------------------------------------------------
    // Copy the used portion of the output buffer to the staging buffer and
    // map it for CPU readback.
    // -----------------------------------------------------------------------
    UINT outputBytes = static_cast<UINT>(totalPixels) * 4u;

    D3D11_BOX copyBox = {};
    copyBox.left  = 0;
    copyBox.right = outputBytes;
    copyBox.top   = 0; copyBox.bottom = 1;
    copyBox.front = 0; copyBox.back   = 1;
    context->CopySubresourceRegion(m_OutputStagingBuffer, 0,
                                   0, 0, 0,
                                   m_OutputBuffer, 0,
                                   &copyBox);

    D3D11_MAPPED_SUBRESOURCE mapped;
    hr = context->Map(m_OutputStagingBuffer, 0, D3D11_MAP_READ, 0, &mapped);
    if (FAILED(hr)) return false;

    outPixels.assign(
        static_cast<const BYTE*>(mapped.pData),
        static_cast<const BYTE*>(mapped.pData) + outputBytes);

    context->Unmap(m_OutputStagingBuffer, 0);
    return true;
}

