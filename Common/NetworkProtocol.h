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
    PACKET_TYPE_INIT  = 1,
    PACKET_TYPE_FRAME = 2,
    PACKET_TYPE_MOUSE_INPUT  = 3,   // Client -> Server
    PACKET_TYPE_CURSOR_SHAPE = 4,   // Server -> Client
    PACKET_TYPE_KEYBOARD_INPUT = 5  // Client -> Server
};

enum MouseInputType : UINT8
{
    MOUSE_INPUT_MOVE            = 0,
    MOUSE_INPUT_LBUTTON_DOWN    = 1,
    MOUSE_INPUT_LBUTTON_UP      = 2,
    MOUSE_INPUT_RBUTTON_DOWN    = 3,
    MOUSE_INPUT_RBUTTON_UP      = 4,
    MOUSE_INPUT_MBUTTON_DOWN    = 5,
    MOUSE_INPUT_MBUTTON_UP      = 6,
    MOUSE_INPUT_WHEEL           = 7,
    MOUSE_INPUT_LBUTTON_DBLCLK  = 8,
    MOUSE_INPUT_RBUTTON_DBLCLK  = 9
};

enum KeyboardInputFlags : UINT8
{
    KEYBOARD_INPUT_FLAG_KEYUP    = 1 << 0,
    KEYBOARD_INPUT_FLAG_EXTENDED = 1 << 1,
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

struct MouseInputPacket
{
    UINT8  InputType;       // MouseInputType
    INT32  X;               // Server screen coordinate X
    INT32  Y;               // Server screen coordinate Y
    INT32  WheelDelta;      // Only valid for MOUSE_INPUT_WHEEL
};

struct KeyboardInputPacket
{
    UINT16 VirtualKey;      // Windows virtual-key code (VK_*)
    UINT16 ScanCode;        // Hardware scan code from lParam
    UINT16 RepeatCount;     // Repeat count from lParam, 1 for normal key event
    UINT8  Flags;           // KeyboardInputFlags
};

struct CursorShapePacket
{
    UINT32 CursorId;        // Cursor handle hash for caching
    UINT32 Width;
    UINT32 Height;
    UINT32 Pitch;
    UINT32 Type;            // DXGI_OUTDUPL_POINTER_SHAPE_TYPE_*
    INT32  HotspotX;
    INT32  HotspotY;
    UINT32 ShapeBufferSize; // 0 means use cached shape
    // Followed by: BYTE ShapeBuffer[ShapeBufferSize]
};
#pragma pack(pop)

#endif
