// THIS CODE AND INFORMATION IS PROVIDED "AS IS" WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING BUT NOT LIMITED TO
// THE IMPLIED WARRANTIES OF MERCHANTABILITY AND/OR FITNESS FOR A
// PARTICULAR PURPOSE.
//
// Copyright (c) Microsoft Corporation. All rights reserved

#ifndef _MOUSECONTROLLER_H_
#define _MOUSECONTROLLER_H_

#include <windows.h>
#include <vector>
#include "../Common/NetworkProtocol.h"

class MouseController
{
public:
    MouseController();

    void SetScreenSize(UINT32 width, UINT32 height);
    void ProcessMouseInput(const MouseInputPacket& input);
    bool GetCurrentCursorShape(CursorShapePacket& outPacket, std::vector<BYTE>& outShapeData);

private:
    UINT32  m_ScreenWidth;
    UINT32  m_ScreenHeight;
    HCURSOR m_LastCursor;
};

#endif
