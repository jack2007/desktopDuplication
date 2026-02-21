// THIS CODE AND INFORMATION IS PROVIDED "AS IS" WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING BUT NOT LIMITED TO
// THE IMPLIED WARRANTIES OF MERCHANTABILITY AND/OR FITNESS FOR A
// PARTICULAR PURPOSE.
//
// Copyright (c) Microsoft Corporation. All rights reserved

#include "TcpClient.h"
#include <iostream>

TcpClient::TcpClient() : m_ConnectSocket(INVALID_SOCKET), m_Initialized(false)
{
}

TcpClient::~TcpClient()
{
    Disconnect();
    if (m_Initialized)
    {
        WSACleanup();
    }
}

bool TcpClient::Connect(const char* ipAddress, int port)
{
    WSADATA wsaData;
    int iResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (iResult != 0)
    {
        return false;
    }
    m_Initialized = true;

    struct addrinfo* result = NULL;
    struct addrinfo hints;

    ZeroMemory(&hints, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    char portStr[16];
    sprintf_s(portStr, "%d", port);

    iResult = getaddrinfo(ipAddress, portStr, &hints, &result);
    if (iResult != 0)
    {
        return false;
    }

    for (struct addrinfo* ptr = result; ptr != NULL; ptr = ptr->ai_next)
    {
        m_ConnectSocket = socket(ptr->ai_family, ptr->ai_socktype, ptr->ai_protocol);
        if (m_ConnectSocket == INVALID_SOCKET)
        {
            continue;
        }

        iResult = connect(m_ConnectSocket, ptr->ai_addr, (int)ptr->ai_addrlen);
        if (iResult == SOCKET_ERROR)
        {
            closesocket(m_ConnectSocket);
            m_ConnectSocket = INVALID_SOCKET;
            continue;
        }
        break;
    }

    freeaddrinfo(result);

    if (m_ConnectSocket == INVALID_SOCKET)
    {
        return false;
    }

    return true;
}

bool TcpClient::SendData(const void* data, size_t size)
{
    if (m_ConnectSocket == INVALID_SOCKET) return false;

    const char* ptr = static_cast<const char*>(data);
    size_t remaining = size;

    while (remaining > 0)
    {
        int sent = send(m_ConnectSocket, ptr, (int)remaining, 0);
        if (sent == SOCKET_ERROR)
        {
            return false;
        }
        ptr += sent;
        remaining -= sent;
    }

    return true;
}

bool TcpClient::ReceiveData(void* buffer, size_t size)
{
    if (m_ConnectSocket == INVALID_SOCKET) return false;

    char* ptr = static_cast<char*>(buffer);
    size_t remaining = size;

    while (remaining > 0)
    {
        int received = recv(m_ConnectSocket, ptr, (int)remaining, 0);
        if (received == SOCKET_ERROR || received == 0)
        {
            return false;
        }
        ptr += received;
        remaining -= received;
    }

    return true;
}

bool TcpClient::HasData()
{
    if (m_ConnectSocket == INVALID_SOCKET) return false;

    fd_set readfds;
    FD_ZERO(&readfds);
    FD_SET(m_ConnectSocket, &readfds);

    timeval timeout;
    timeout.tv_sec = 0;
    timeout.tv_usec = 10000; // 10ms timeout

    int result = select(0, &readfds, NULL, NULL, &timeout);
    return result > 0;
}

void TcpClient::Disconnect()
{
    if (m_ConnectSocket != INVALID_SOCKET)
    {
        shutdown(m_ConnectSocket, SD_BOTH);
        closesocket(m_ConnectSocket);
        m_ConnectSocket = INVALID_SOCKET;
    }
}
