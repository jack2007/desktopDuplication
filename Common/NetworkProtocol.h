// THIS CODE AND INFORMATION IS PROVIDED "AS IS" WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING BUT NOT LIMITED TO
// THE IMPLIED WARRANTIES OF MERCHANTABILITY AND/OR FITNESS FOR A
// PARTICULAR PURPOSE.
//
// Copyright (c) Microsoft Corporation. All rights reserved

#ifndef _NETWORKPROTOCOL_H_
#define _NETWORKPROTOCOL_H_

#include <windows.h>
#include <dxgi1_2.h>

#define DEFAULT_SERVER_PORT 12306
#define PACKET_MAGIC_NUMBER 0x4455504C // "DUPL"

enum PacketType
{
    PACKET_TYPE_INIT = 1,
    PACKET_TYPE_FRAME = 2
};

#pragma pack(push, 1)
struct PacketHeader
{
    UINT32 MagicNumber;
    UINT32 Type;
    UINT32 CompressedSize;
    UINT32 UncompressedSize;
};

struct InitPacket
{
    UINT32 Width;
    UINT32 Height;
    DXGI_FORMAT Format;
};

struct FramePacketHeader
{
    UINT32 DirtyRectCount;
    UINT32 MoveRectCount;
    bool HasPointerInfo;
    // When true the pixel payload is a flat dense-packed array of dirty-rect
    // pixels (one contiguous block per dirty rect, in scanline order).
    // The client must use UpdateSubresource per rect rather than treating the
    // data as a full-frame buffer.
    bool IsDensePacked;
    // Followed by:
    // RECT DirtyRects[DirtyRectCount]
    // DXGI_OUTDUPL_MOVE_RECT MoveRects[MoveRectCount]
    // PTR_INFO PointerInfo (if HasPointerInfo is true)
    // Pixel Data (densely packed dirty-rect pixels when IsDensePacked is true)
};
#pragma pack(pop)

#endif
