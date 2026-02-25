// THIS CODE AND INFORMATION IS PROVIDED "AS IS" WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING BUT NOT LIMITED TO
// THE IMPLIED WARRANTIES OF MERCHANTABILITY AND/OR FITNESS FOR A
// PARTICULAR PURPOSE.
//
// Copyright (c) Microsoft Corporation. All rights reserved

#include "TcpServer.h"
#include <iostream>

TcpServer::TcpServer() : m_ListenSocket(INVALID_SOCKET), m_ClientSocket(INVALID_SOCKET), m_Initialized(false)
{
}

TcpServer::~TcpServer()
{
    Disconnect();
    if (m_ListenSocket != INVALID_SOCKET)
    {
        closesocket(m_ListenSocket);
    }
    if (m_Initialized)
    {
        WSACleanup();
    }
}

bool TcpServer::Initialize(int port)
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
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    hints.ai_flags = AI_PASSIVE;

    char portStr[16];
    sprintf_s(portStr, "%d", port);

    iResult = getaddrinfo(NULL, portStr, &hints, &result);
    if (iResult != 0)
    {
        return false;
    }

    m_ListenSocket = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
    if (m_ListenSocket == INVALID_SOCKET)
    {
        freeaddrinfo(result);
        return false;
    }

    iResult = bind(m_ListenSocket, result->ai_addr, (int)result->ai_addrlen);
    if (iResult == SOCKET_ERROR)
    {
        freeaddrinfo(result);
        closesocket(m_ListenSocket);
        m_ListenSocket = INVALID_SOCKET;
        return false;
    }

    freeaddrinfo(result);

    iResult = listen(m_ListenSocket, SOMAXCONN);
    if (iResult == SOCKET_ERROR)
    {
        closesocket(m_ListenSocket);
        m_ListenSocket = INVALID_SOCKET;
        return false;
    }

    return true;
}

bool TcpServer::WaitForClient()
{
    if (m_ListenSocket == INVALID_SOCKET) return false;

    m_ClientSocket = accept(m_ListenSocket, NULL, NULL);
    if (m_ClientSocket == INVALID_SOCKET)
    {
        return false;
    }

    return true;
}

bool TcpServer::SendData(const void* data, size_t size)
{
    if (m_ClientSocket == INVALID_SOCKET) return false;

    const char* ptr = static_cast<const char*>(data);
    size_t remaining = size;

    while (remaining > 0)
    {
        int sent = send(m_ClientSocket, ptr, (int)remaining, 0);
        if (sent == SOCKET_ERROR)
        {
            return false;
        }
        ptr += sent;
        remaining -= sent;
    }

    return true;
}

bool TcpServer::ReceiveData(void* buffer, size_t size)
{
    if (m_ClientSocket == INVALID_SOCKET) return false;

    char* ptr = static_cast<char*>(buffer);
    size_t remaining = size;

    while (remaining > 0)
    {
        int received = recv(m_ClientSocket, ptr, (int)remaining, 0);
        if (received == SOCKET_ERROR || received == 0)
        {
            return false;
        }
        ptr += received;
        remaining -= received;
    }

    return true;
}

bool TcpServer::HasData()
{
    if (m_ClientSocket == INVALID_SOCKET) return false;
    fd_set readfds;
    FD_ZERO(&readfds);
    FD_SET(m_ClientSocket, &readfds);
    timeval timeout;
    timeout.tv_sec = 0;
    timeout.tv_usec = 0; // non-blocking check
    int result = select(0, &readfds, NULL, NULL, &timeout);
    return result > 0;
}

void TcpServer::Disconnect()
{
    if (m_ClientSocket != INVALID_SOCKET)
    {
        shutdown(m_ClientSocket, SD_BOTH);
        closesocket(m_ClientSocket);
        m_ClientSocket = INVALID_SOCKET;
    }
}
