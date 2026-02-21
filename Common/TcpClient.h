// THIS CODE AND INFORMATION IS PROVIDED "AS IS" WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING BUT NOT LIMITED TO
// THE IMPLIED WARRANTIES OF MERCHANTABILITY AND/OR FITNESS FOR A
// PARTICULAR PURPOSE.
//
// Copyright (c) Microsoft Corporation. All rights reserved

#ifndef _TCPCLIENT_H_
#define _TCPCLIENT_H_

#include <winsock2.h>
#include <ws2tcpip.h>
#include <vector>

#pragma comment(lib, "Ws2_32.lib")

class TcpClient
{
public:
    TcpClient();
    ~TcpClient();

    bool Connect(const char* ipAddress, int port);
    bool SendData(const void* data, size_t size);
    bool ReceiveData(void* buffer, size_t size);
    bool HasData();
    void Disconnect();

private:
    SOCKET m_ConnectSocket;
    bool m_Initialized;
};

#endif
