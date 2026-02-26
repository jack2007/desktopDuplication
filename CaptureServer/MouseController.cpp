// THIS CODE AND INFORMATION IS PROVIDED "AS IS" WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING BUT NOT LIMITED TO
// THE IMPLIED WARRANTIES OF MERCHANTABILITY AND/OR FITNESS FOR A
// PARTICULAR PURPOSE.
//
// Copyright (c) Microsoft Corporation. All rights reserved

#include "MouseController.h"
#include <dxgi1_2.h>
#include "Logger.h"

MouseController::MouseController()
    : m_CaptureLeft(0), m_CaptureTop(0), m_ScreenWidth(0), m_ScreenHeight(0), m_LastCursor(nullptr)
{
}

void MouseController::SetScreenSize(UINT32 width, UINT32 height)
{
    SetCaptureArea(0, 0, width, height);
}

void MouseController::SetCaptureArea(INT32 left, INT32 top, UINT32 width, UINT32 height)
{
    m_CaptureLeft = left;
    m_CaptureTop = top;
    m_ScreenWidth = width;
    m_ScreenHeight = height;
    m_LastCursor = nullptr; // Force full cursor shape re-send on new connection
    LOG_INFO("MouseController::SetCaptureArea left={}, top={}, width={}, height={}",
             m_CaptureLeft, m_CaptureTop, m_ScreenWidth, m_ScreenHeight);
}

void MouseController::ProcessMouseInput(const MouseInputPacket& input)
{
    if (m_ScreenWidth == 0 || m_ScreenHeight == 0) return;

    // Client sends coordinates relative to the captured output.
    INT32 relX = input.X;
    INT32 relY = input.Y;
    relX = max(0, min(relX, static_cast<INT32>(m_ScreenWidth) - 1));
    relY = max(0, min(relY, static_cast<INT32>(m_ScreenHeight) - 1));
    INT32 desktopX = m_CaptureLeft + relX;
    INT32 desktopY = m_CaptureTop + relY;

    // Map to virtual-desktop absolute coordinates used by SendInput fallback.
    INT32 virtualLeft   = GetSystemMetrics(SM_XVIRTUALSCREEN);
    INT32 virtualTop    = GetSystemMetrics(SM_YVIRTUALSCREEN);
    INT32 virtualWidth  = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    INT32 virtualHeight = GetSystemMetrics(SM_CYVIRTUALSCREEN);

    auto sendMoveToTarget = [&]() -> bool
    {
        // Prefer SetCursorPos for robustness across reconnect/session transitions.
        if (SetCursorPos(desktopX, desktopY))
        {
            return true;
        }

        // Fallback to SendInput absolute move on virtual desktop.
        if (virtualWidth > 1 && virtualHeight > 1)
        {
            LONG absX = static_cast<LONG>(
                static_cast<long long>(desktopX - virtualLeft) * 65535LL /
                static_cast<long long>(virtualWidth - 1));
            LONG absY = static_cast<LONG>(
                static_cast<long long>(desktopY - virtualTop) * 65535LL /
                static_cast<long long>(virtualHeight - 1));

            INPUT move = {};
            move.type = INPUT_MOUSE;
            move.mi.dx = absX;
            move.mi.dy = absY;
            move.mi.dwFlags = MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK | MOUSEEVENTF_MOVE;
            return SendInput(1, &move, sizeof(INPUT)) == 1;
        }

        return false;
    };

    auto sendButtonEvent = [&](DWORD buttonFlags, DWORD mouseData = 0) -> bool
    {
        INPUT btn = {};
        btn.type = INPUT_MOUSE;
        btn.mi.dwFlags = buttonFlags;
        btn.mi.mouseData = mouseData;
        return SendInput(1, &btn, sizeof(INPUT)) == 1;
    };

    switch (static_cast<MouseInputType>(input.InputType))
    {
        case MOUSE_INPUT_MOVE:
            if (!sendMoveToTarget())
            {
                LOG_WARN("MouseController::ProcessMouseInput MOVE SendInput failed, gle={}", GetLastError());
            }
            break;
        case MOUSE_INPUT_LBUTTON_DOWN:
            if (!sendMoveToTarget())
            {
                LOG_WARN("MouseController::ProcessMouseInput LBUTTON_DOWN move failed, gle={}", GetLastError());
            }
            if (!sendButtonEvent(MOUSEEVENTF_LEFTDOWN))
            {
                LOG_WARN("MouseController::ProcessMouseInput LBUTTON_DOWN SendInput failed, gle={}", GetLastError());
            }
            break;
        case MOUSE_INPUT_LBUTTON_UP:
            if (!sendButtonEvent(MOUSEEVENTF_LEFTUP))
            {
                LOG_WARN("MouseController::ProcessMouseInput LBUTTON_UP SendInput failed, gle={}", GetLastError());
            }
            break;
        case MOUSE_INPUT_RBUTTON_DOWN:
            if (!sendMoveToTarget())
            {
                LOG_WARN("MouseController::ProcessMouseInput RBUTTON_DOWN move failed, gle={}", GetLastError());
            }
            if (!sendButtonEvent(MOUSEEVENTF_RIGHTDOWN))
            {
                LOG_WARN("MouseController::ProcessMouseInput RBUTTON_DOWN SendInput failed, gle={}", GetLastError());
            }
            break;
        case MOUSE_INPUT_RBUTTON_UP:
            if (!sendButtonEvent(MOUSEEVENTF_RIGHTUP))
            {
                LOG_WARN("MouseController::ProcessMouseInput RBUTTON_UP SendInput failed, gle={}", GetLastError());
            }
            break;
        case MOUSE_INPUT_MBUTTON_DOWN:
            if (!sendMoveToTarget())
            {
                LOG_WARN("MouseController::ProcessMouseInput MBUTTON_DOWN move failed, gle={}", GetLastError());
            }
            if (!sendButtonEvent(MOUSEEVENTF_MIDDLEDOWN))
            {
                LOG_WARN("MouseController::ProcessMouseInput MBUTTON_DOWN SendInput failed, gle={}", GetLastError());
            }
            break;
        case MOUSE_INPUT_MBUTTON_UP:
            if (!sendButtonEvent(MOUSEEVENTF_MIDDLEUP))
            {
                LOG_WARN("MouseController::ProcessMouseInput MBUTTON_UP SendInput failed, gle={}", GetLastError());
            }
            break;
        case MOUSE_INPUT_WHEEL:
            if (!sendMoveToTarget() || !sendButtonEvent(MOUSEEVENTF_WHEEL, static_cast<DWORD>(input.WheelDelta)))
            {
                LOG_WARN("MouseController::ProcessMouseInput WHEEL SendInput failed, gle={}", GetLastError());
            }
            break;
        case MOUSE_INPUT_LBUTTON_DBLCLK:
        {
            INPUT dblClk[4] = {};
            if (!sendMoveToTarget())
            {
                LOG_WARN("MouseController::ProcessMouseInput LBUTTON_DBLCLK move failed, gle={}", GetLastError());
            }
            for (int i = 0; i < 4; ++i)
            {
                dblClk[i].type = INPUT_MOUSE;
            }
            dblClk[0].mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
            dblClk[1].mi.dwFlags = MOUSEEVENTF_LEFTUP;
            dblClk[2].mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
            dblClk[3].mi.dwFlags = MOUSEEVENTF_LEFTUP;
            if (SendInput(4, dblClk, sizeof(INPUT)) != 4)
            {
                LOG_WARN("MouseController::ProcessMouseInput LBUTTON_DBLCLK SendInput failed, gle={}", GetLastError());
            }
            break;
        }
        case MOUSE_INPUT_RBUTTON_DBLCLK:
        {
            INPUT dblClk[4] = {};
            if (!sendMoveToTarget())
            {
                LOG_WARN("MouseController::ProcessMouseInput RBUTTON_DBLCLK move failed, gle={}", GetLastError());
            }
            for (int i = 0; i < 4; ++i)
            {
                dblClk[i].type = INPUT_MOUSE;
            }
            dblClk[0].mi.dwFlags = MOUSEEVENTF_RIGHTDOWN;
            dblClk[1].mi.dwFlags = MOUSEEVENTF_RIGHTUP;
            dblClk[2].mi.dwFlags = MOUSEEVENTF_RIGHTDOWN;
            dblClk[3].mi.dwFlags = MOUSEEVENTF_RIGHTUP;
            if (SendInput(4, dblClk, sizeof(INPUT)) != 4)
            {
                LOG_WARN("MouseController::ProcessMouseInput RBUTTON_DBLCLK SendInput failed, gle={}", GetLastError());
            }
            break;
        }
        default:
            // Defensive fallback: if packet type is unknown, at least move cursor
            // to keep remote control responsive while we diagnose packet mismatch.
            LOG_WARN("MouseController::ProcessMouseInput unknown input type={}, x={}, y={}",
                     static_cast<unsigned>(input.InputType), input.X, input.Y);
            if (!sendMoveToTarget())
            {
                LOG_WARN("MouseController::ProcessMouseInput UNKNOWN SendInput MOVE failed, gle={}", GetLastError());
            }
            break;
    }
}

bool MouseController::GetCurrentCursorShape(CursorShapePacket& outPacket, std::vector<BYTE>& outShapeData)
{
    HCURSOR fallbackCursor = LoadCursor(nullptr, IDC_ARROW);
    HCURSOR hCursor = nullptr;
    bool usedFallback = false;

    CURSORINFO cursorInfo = {};
    cursorInfo.cbSize = sizeof(CURSORINFO);
    if (GetCursorInfo(&cursorInfo) &&
        cursorInfo.hCursor != NULL &&
        (cursorInfo.flags & CURSOR_SHOWING))
    {
        hCursor = cursorInfo.hCursor;
    }
    else
    {
        DWORD gle = GetLastError();
        if (!fallbackCursor)
        {
            LOG_WARN("MouseController::GetCurrentCursorShape: no system cursor and fallback load failed, gle={}", gle);
            return false;
        }
        hCursor = fallbackCursor;
        usedFallback = true;
    }

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
    if (!GetIconInfo(hCursor, &iconInfo))
    {
        DWORD gle = GetLastError();
        if (!usedFallback && fallbackCursor && fallbackCursor != hCursor && GetIconInfo(fallbackCursor, &iconInfo))
        {
            hCursor = fallbackCursor;
            usedFallback = true;
            outPacket.CursorId = static_cast<UINT32>(reinterpret_cast<ULONG_PTR>(hCursor) & 0xFFFFFFFF);
            LOG_WARN("MouseController::GetCurrentCursorShape: GetIconInfo failed on system cursor, switched to fallback, gle={}", gle);
        }
        else
        {
            LOG_WARN("MouseController::GetCurrentCursorShape: GetIconInfo failed, gle={}", gle);
            return false;
        }
    }

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
