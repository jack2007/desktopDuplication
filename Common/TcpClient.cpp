// THIS CODE AND INFORMATION IS PROVIDED "AS IS" WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING BUT NOT LIMITED TO
// THE IMPLIED WARRANTIES OF MERCHANTABILITY AND/OR FITNESS FOR A
// PARTICULAR PURPOSE.
//
// Copyright (c) Microsoft Corporation. All rights reserved

#include "TcpClient.h"
#include <iostream>
#include <CommonTypes.h>

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
        ProcessFailure(nullptr, L"WSAStartup Failed", L"Error", E_FAIL);
        return false;
    }
    m_Initialized = true;

    // 创建socket
    m_ConnectSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (m_ConnectSocket == INVALID_SOCKET)
    {
        ProcessFailure(nullptr, L"socket Failed", L"Error", E_FAIL);
        return false;
    }

	// 构造服务器地址
	struct sockaddr_in serverAddr;
    ZeroMemory(&serverAddr, sizeof(serverAddr));
	serverAddr.sin_family = AF_INET;
	serverAddr.sin_port = htons(port);
	serverAddr.sin_addr.s_addr = inet_addr(ipAddress);
    iResult = connect(m_ConnectSocket, (const sockaddr *) & serverAddr, sizeof(serverAddr));
    if (iResult == SOCKET_ERROR)
    {
        ProcessFailure(nullptr, L"connect Failed", L"Error", E_FAIL);
        closesocket(m_ConnectSocket);
        m_ConnectSocket = INVALID_SOCKET;

    }

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
