// THIS CODE AND INFORMATION IS PROVIDED "AS IS" WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING BUT NOT LIMITED TO
// THE IMPLIED WARRANTIES OF MERCHANTABILITY AND/OR FITNESS FOR A
// PARTICULAR PURPOSE.
//
// Copyright (c) Microsoft Corporation. All rights reserved

// RLECompress.hlsl
// Compute shader: Densely pack dirty-rect pixels from a desktop frame texture
// into a flat linear output buffer, eliminating the full-frame PCIe copy.
//
// Dispatch: (ceil(maxRectW/8), ceil(maxRectH/8), dirtyRectCount)
// Each thread handles one pixel inside its assigned dirty rect.

struct DirtyRectInfo
{
    int left;
    int top;
    int right;
    int bottom;
    int outputOffset; // pixel index (not byte offset) of first pixel for this rect
    int pad0;
    int pad1;
    int pad2;
};

Texture2D<float4>               InputTexture : register(t0);
StructuredBuffer<DirtyRectInfo> DirtyRects   : register(t1);
RWByteAddressBuffer             OutputBuffer : register(u0);

cbuffer Constants : register(b0)
{
    uint DirtyRectCount;
    uint pad0;
    uint pad1;
    uint pad2;
};

[numthreads(8, 8, 1)]
void CSMain(uint3 GroupID       : SV_GroupID,
            uint3 GroupThreadID : SV_GroupThreadID)
{
    uint rectIdx = GroupID.z;
    if (rectIdx >= DirtyRectCount)
        return;

    DirtyRectInfo rect = DirtyRects[rectIdx];

    uint localX = GroupID.x * 8u + GroupThreadID.x;
    uint localY = GroupID.y * 8u + GroupThreadID.y;

    uint rectW = (uint)(rect.right  - rect.left);
    uint rectH = (uint)(rect.bottom - rect.top);

    if (localX >= rectW || localY >= rectH)
        return;

    int px = rect.left + (int)localX;
    int py = rect.top  + (int)localY;

    // Load pixel from texture.
    // For DXGI_FORMAT_B8G8R8A8_UNORM the HLSL float4 components map as:
    //   x = Blue  (byte 0 in memory)
    //   y = Green (byte 1 in memory)
    //   z = Red   (byte 2 in memory)
    //   w = Alpha (byte 3 in memory)
    float4 c = InputTexture.Load(int3(px, py, 0));

    // Pack back to uint32 preserving BGRA byte order so the client can write
    // the raw bytes directly into a DXGI_FORMAT_B8G8R8A8_UNORM texture via
    // UpdateSubresource without any swizzle.
    uint b = (uint)(saturate(c.x) * 255.0f + 0.5f);
    uint g = (uint)(saturate(c.y) * 255.0f + 0.5f);
    uint r = (uint)(saturate(c.z) * 255.0f + 0.5f);
    uint a = (uint)(saturate(c.w) * 255.0f + 0.5f);
    uint packed = b | (g << 8u) | (r << 16u) | (a << 24u);

    // Write to output buffer (byte address = pixelIndex * 4).
    uint outPixelIdx = (uint)rect.outputOffset + localY * rectW + localX;
    OutputBuffer.Store(outPixelIdx * 4u, packed);
}
