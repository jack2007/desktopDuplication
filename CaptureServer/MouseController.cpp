// THIS CODE AND INFORMATION IS PROVIDED "AS IS" WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING BUT NOT LIMITED TO
// THE IMPLIED WARRANTIES OF MERCHANTABILITY AND/OR FITNESS FOR A
// PARTICULAR PURPOSE.
//
// Copyright (c) Microsoft Corporation. All rights reserved

#include "MouseController.h"
#include <dxgi1_2.h>

MouseController::MouseController() : m_ScreenWidth(0), m_ScreenHeight(0), m_LastCursor(nullptr)
{
}

void MouseController::SetScreenSize(UINT32 width, UINT32 height)
{
    m_ScreenWidth = width;
    m_ScreenHeight = height;
    m_LastCursor = nullptr; // Force full cursor shape re-send on new connection
}

void MouseController::ProcessMouseInput(const MouseInputPacket& input)
{
    if (m_ScreenWidth == 0 || m_ScreenHeight == 0) return;

    // Convert screen coordinates to absolute SendInput coordinates (0-65536 range)
    LONG absX = static_cast<LONG>(static_cast<long long>(input.X) * 65536 / static_cast<long long>(m_ScreenWidth));
    LONG absY = static_cast<LONG>(static_cast<long long>(input.Y) * 65536 / static_cast<long long>(m_ScreenHeight));

    INPUT inp = {};
    inp.type = INPUT_MOUSE;
    inp.mi.dx = absX;
    inp.mi.dy = absY;
    inp.mi.dwFlags = MOUSEEVENTF_ABSOLUTE;

    switch (static_cast<MouseInputType>(input.InputType))
    {
        case MOUSE_INPUT_MOVE:
            inp.mi.dwFlags |= MOUSEEVENTF_MOVE;
            SendInput(1, &inp, sizeof(INPUT));
            break;
        case MOUSE_INPUT_LBUTTON_DOWN:
            inp.mi.dwFlags |= MOUSEEVENTF_MOVE | MOUSEEVENTF_LEFTDOWN;
            SendInput(1, &inp, sizeof(INPUT));
            break;
        case MOUSE_INPUT_LBUTTON_UP:
            inp.mi.dwFlags |= MOUSEEVENTF_MOVE | MOUSEEVENTF_LEFTUP;
            SendInput(1, &inp, sizeof(INPUT));
            break;
        case MOUSE_INPUT_RBUTTON_DOWN:
            inp.mi.dwFlags |= MOUSEEVENTF_MOVE | MOUSEEVENTF_RIGHTDOWN;
            SendInput(1, &inp, sizeof(INPUT));
            break;
        case MOUSE_INPUT_RBUTTON_UP:
            inp.mi.dwFlags |= MOUSEEVENTF_MOVE | MOUSEEVENTF_RIGHTUP;
            SendInput(1, &inp, sizeof(INPUT));
            break;
        case MOUSE_INPUT_MBUTTON_DOWN:
            inp.mi.dwFlags |= MOUSEEVENTF_MOVE | MOUSEEVENTF_MIDDLEDOWN;
            SendInput(1, &inp, sizeof(INPUT));
            break;
        case MOUSE_INPUT_MBUTTON_UP:
            inp.mi.dwFlags |= MOUSEEVENTF_MOVE | MOUSEEVENTF_MIDDLEUP;
            SendInput(1, &inp, sizeof(INPUT));
            break;
        case MOUSE_INPUT_WHEEL:
            inp.mi.dwFlags |= MOUSEEVENTF_WHEEL;
            inp.mi.mouseData = static_cast<DWORD>(input.WheelDelta);
            SendInput(1, &inp, sizeof(INPUT));
            break;
        case MOUSE_INPUT_LBUTTON_DBLCLK:
        {
            INPUT dblClk[4] = {};
            for (int i = 0; i < 4; ++i)
            {
                dblClk[i].type = INPUT_MOUSE;
                dblClk[i].mi.dx = absX;
                dblClk[i].mi.dy = absY;
                dblClk[i].mi.dwFlags = MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_MOVE;
            }
            dblClk[0].mi.dwFlags |= MOUSEEVENTF_LEFTDOWN;
            dblClk[1].mi.dwFlags |= MOUSEEVENTF_LEFTUP;
            dblClk[2].mi.dwFlags |= MOUSEEVENTF_LEFTDOWN;
            dblClk[3].mi.dwFlags |= MOUSEEVENTF_LEFTUP;
            SendInput(4, dblClk, sizeof(INPUT));
            break;
        }
        case MOUSE_INPUT_RBUTTON_DBLCLK:
        {
            INPUT dblClk[4] = {};
            for (int i = 0; i < 4; ++i)
            {
                dblClk[i].type = INPUT_MOUSE;
                dblClk[i].mi.dx = absX;
                dblClk[i].mi.dy = absY;
                dblClk[i].mi.dwFlags = MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_MOVE;
            }
            dblClk[0].mi.dwFlags |= MOUSEEVENTF_RIGHTDOWN;
            dblClk[1].mi.dwFlags |= MOUSEEVENTF_RIGHTUP;
            dblClk[2].mi.dwFlags |= MOUSEEVENTF_RIGHTDOWN;
            dblClk[3].mi.dwFlags |= MOUSEEVENTF_RIGHTUP;
            SendInput(4, dblClk, sizeof(INPUT));
            break;
        }
        default:
            break;
    }
}

bool MouseController::GetCurrentCursorShape(CursorShapePacket& outPacket, std::vector<BYTE>& outShapeData)
{
    CURSORINFO cursorInfo = {};
    cursorInfo.cbSize = sizeof(CURSORINFO);
    if (!GetCursorInfo(&cursorInfo)) return false;
    if (cursorInfo.hCursor == NULL) return false;

    HCURSOR hCursor = cursorInfo.hCursor;
    UINT32 cursorId = static_cast<UINT32>(reinterpret_cast<ULONG_PTR>(hCursor) & 0xFFFFFFFF);

    outPacket.CursorId = cursorId;

    // If cursor hasn't changed, send with ShapeBufferSize = 0 (client uses cached shape)
    if (hCursor == m_LastCursor)
    {
        outPacket.ShapeBufferSize = 0;
        outPacket.Width    = 0;
        outPacket.Height   = 0;
        outPacket.Pitch    = 0;
        outPacket.Type     = 0;
        outPacket.HotspotX = 0;
        outPacket.HotspotY = 0;
        return true;
    }

    ICONINFO iconInfo = {};
    if (!GetIconInfo(hCursor, &iconInfo)) return false;

    BITMAP bm = {};
    if (!GetObject(iconInfo.hbmMask, sizeof(BITMAP), &bm))
    {
        DeleteObject(iconInfo.hbmMask);
        if (iconInfo.hbmColor) DeleteObject(iconInfo.hbmColor);
        return false;
    }

    bool isColor = (iconInfo.hbmColor != NULL);
    UINT32 cursorWidth = static_cast<UINT32>(bm.bmWidth);

    HDC hDC = GetDC(NULL);
    bool success = false;

    if (isColor)
    {
        BITMAP bmColor = {};
        GetObject(iconInfo.hbmColor, sizeof(BITMAP), &bmColor);
        UINT32 cursorHeight = static_cast<UINT32>(abs(bmColor.bmHeight));
        UINT32 pitch = cursorWidth * 4;

        outPacket.Width    = cursorWidth;
        outPacket.Height   = cursorHeight;
        outPacket.Pitch    = pitch;
        outPacket.Type     = DXGI_OUTDUPL_POINTER_SHAPE_TYPE_COLOR;
        outPacket.HotspotX = static_cast<INT32>(iconInfo.xHotspot);
        outPacket.HotspotY = static_cast<INT32>(iconInfo.yHotspot);

        outShapeData.assign(static_cast<size_t>(cursorHeight) * pitch, 0);

        BITMAPINFO bmi = {};
        bmi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth       = static_cast<LONG>(cursorWidth);
        bmi.bmiHeader.biHeight      = -static_cast<LONG>(cursorHeight); // top-down
        bmi.bmiHeader.biPlanes      = 1;
        bmi.bmiHeader.biBitCount    = 32;
        bmi.bmiHeader.biCompression = BI_RGB;

        int linesGot = GetDIBits(hDC, iconInfo.hbmColor, 0, cursorHeight, outShapeData.data(), &bmi, DIB_RGB_COLORS);
        success = (linesGot > 0);

        if (success)
        {
            // Retrieve AND mask to populate alpha channel
            // AND mask: 0 = opaque pixel, 1 = transparent pixel
            UINT32 maskPitch  = ((cursorWidth + 31) / 32) * 4;
            UINT32 maskHeight = cursorHeight;
            std::vector<BYTE> maskData(static_cast<size_t>(maskHeight) * maskPitch, 0);

            struct { BITMAPINFOHEADER bmiHeader; RGBQUAD bmiColors[2]; } maskBmi = {};
            maskBmi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
            maskBmi.bmiHeader.biWidth       = static_cast<LONG>(cursorWidth);
            maskBmi.bmiHeader.biHeight      = -static_cast<LONG>(maskHeight);
            maskBmi.bmiHeader.biPlanes      = 1;
            maskBmi.bmiHeader.biBitCount    = 1;
            maskBmi.bmiHeader.biCompression = BI_RGB;
            maskBmi.bmiColors[0] = { 0,   0,   0,   0 };
            maskBmi.bmiColors[1] = { 255, 255, 255, 0 };

            GetDIBits(hDC, iconInfo.hbmMask, 0, maskHeight, maskData.data(),
                      reinterpret_cast<BITMAPINFO*>(&maskBmi), DIB_RGB_COLORS);

            for (UINT32 y = 0; y < cursorHeight; ++y)
            {
                for (UINT32 x = 0; x < cursorWidth; ++x)
                {
                    UINT32 maskByteIdx = (x / 8) + y * maskPitch;
                    UINT32 maskBit = (maskData[maskByteIdx] >> (7u - (x % 8u))) & 1u;
                    UINT32 pixelIdx = y * pitch + x * 4;
                    outShapeData[pixelIdx + 3] = maskBit ? 0x00 : 0xFF;
                }
            }
        }
    }
    else
    {
        // Monochrome cursor: 1bpp AND mask + XOR mask stacked (fullHeight = 2 * cursor_height)
        UINT32 fullHeight = static_cast<UINT32>(bm.bmHeight);
        UINT32 pitch      = ((cursorWidth + 31) / 32) * 4;

        outPacket.Width    = cursorWidth;
        outPacket.Height   = fullHeight;
        outPacket.Pitch    = pitch;
        outPacket.Type     = DXGI_OUTDUPL_POINTER_SHAPE_TYPE_MONOCHROME;
        outPacket.HotspotX = static_cast<INT32>(iconInfo.xHotspot);
        outPacket.HotspotY = static_cast<INT32>(iconInfo.yHotspot);

        outShapeData.assign(static_cast<size_t>(fullHeight) * pitch, 0);

        struct { BITMAPINFOHEADER bmiHeader; RGBQUAD bmiColors[2]; } maskBmi = {};
        maskBmi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
        maskBmi.bmiHeader.biWidth       = static_cast<LONG>(cursorWidth);
        maskBmi.bmiHeader.biHeight      = -static_cast<LONG>(fullHeight);
        maskBmi.bmiHeader.biPlanes      = 1;
        maskBmi.bmiHeader.biBitCount    = 1;
        maskBmi.bmiHeader.biCompression = BI_RGB;
        maskBmi.bmiColors[0] = { 0,   0,   0,   0 };
        maskBmi.bmiColors[1] = { 255, 255, 255, 0 };

        int linesGot = GetDIBits(hDC, iconInfo.hbmMask, 0, fullHeight, outShapeData.data(),
                                 reinterpret_cast<BITMAPINFO*>(&maskBmi), DIB_RGB_COLORS);
        success = (linesGot > 0);
    }

    ReleaseDC(NULL, hDC);
    DeleteObject(iconInfo.hbmMask);
    if (iconInfo.hbmColor) DeleteObject(iconInfo.hbmColor);

    if (success)
    {
        outPacket.ShapeBufferSize = static_cast<UINT32>(outShapeData.size());
        m_LastCursor = hCursor;
    }
    else
    {
        outPacket.ShapeBufferSize = 0;
        outShapeData.clear();
    }

    return success;
}
