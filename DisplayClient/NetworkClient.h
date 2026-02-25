// THIS CODE AND INFORMATION IS PROVIDED "AS IS" WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING BUT NOT LIMITED TO
// THE IMPLIED WARRANTIES OF MERCHANTABILITY AND/OR FITNESS FOR A
// PARTICULAR PURPOSE.
//
// Copyright (c) Microsoft Corporation. All rights reserved

#ifndef _NETWORKCLIENT_H_
#define _NETWORKCLIENT_H_

#include "../Common/TcpClient.h"
#include "../Common/ZstdCompressor.h"
#include "../Common/NetworkProtocol.h"
#include "CommonTypes.h"
#include <vector>

class NetworkClient
{
public:
    NetworkClient();
    ~NetworkClient();

    bool Initialize(const char* ipAddress, int port);
    bool ReceiveInitPacket(InitPacket& outInitData);
    bool ReceiveFramePacket(std::vector<BYTE>& outUncompressedData, FramePacketHeader& outHeader);
    bool HasData();
    void Disconnect();

    bool SendMouseInput(const MouseInputPacket& input);
    bool ReceivePacketHeader(PacketHeader& outHeader);
    bool ReceiveFramePacketBody(const PacketHeader& header, std::vector<BYTE>& outUncompressedData, FramePacketHeader& outFrameHeader);
    bool ReceiveCursorShapeBody(const PacketHeader& header, CursorShapePacket& outPacket, std::vector<BYTE>& outShapeData);

private:
    TcpClient m_Client;
    ZstdCompressor m_Compressor;
};

#endif
