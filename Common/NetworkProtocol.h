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
    UINT8  ProtocolVersion;    // 0 = legacy BGRA, 1 = supports PixelFormatFlags
    UINT8  Reserved[3];        // alignment padding for future use
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
    bool   HasPointerInfo;
    bool   IsDeltaEncoded;   // true: pixel data is XOR diff vs previous frame
    UINT8  PixelFormatFlags; // BIT0 = 1: Alpha stripped, pixel data is BGR 3 bytes/pixel
    UINT8  Reserved;         // alignment padding
    // Followed by:
    // RECT DirtyRects[DirtyRectCount]
    // DXGI_OUTDUPL_MOVE_RECT MoveRects[MoveRectCount]
    // PTR_INFO PointerInfo (if HasPointerInfo is true)
    // Pixel Data (Compressed)
};
#pragma pack(pop)

#endif
