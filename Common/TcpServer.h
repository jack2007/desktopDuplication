// THIS CODE AND INFORMATION IS PROVIDED "AS IS" WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING BUT NOT LIMITED TO
// THE IMPLIED WARRANTIES OF MERCHANTABILITY AND/OR FITNESS FOR A
// PARTICULAR PURPOSE.
//
// Copyright (c) Microsoft Corporation. All rights reserved

#ifndef _TCPSERVER_H_
#define _TCPSERVER_H_

#include <winsock2.h>
#include <ws2tcpip.h>
#include <vector>

#pragma comment(lib, "Ws2_32.lib")

class TcpServer
{
public:
    TcpServer();
    ~TcpServer();

    bool Initialize(int port);
    bool WaitForClient();
    bool SendData(const void* data, size_t size);
    bool ReceiveData(void* buffer, size_t size);
    bool IsConnected() const { return m_ClientSocket != INVALID_SOCKET; }
    void Disconnect();

private:
    SOCKET m_ListenSocket;
    SOCKET m_ClientSocket;
    bool m_Initialized;
};

#endif
