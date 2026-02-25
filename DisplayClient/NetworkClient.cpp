// THIS CODE AND INFORMATION IS PROVIDED "AS IS" WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING BUT NOT LIMITED TO
// THE IMPLIED WARRANTIES OF MERCHANTABILITY AND/OR FITNESS FOR A
// PARTICULAR PURPOSE.
//
// Copyright (c) Microsoft Corporation. All rights reserved

#include "NetworkClient.h"
#include <iostream>

NetworkClient::NetworkClient()
{
}

NetworkClient::~NetworkClient()
{
    Disconnect();
}

bool NetworkClient::Initialize(const char* ipAddress, int port)
{
    if (!m_Compressor.Initialize())
    {
        ProcessFailure(nullptr, L"m_Compressor.Initialize Failed", L"Error", E_FAIL);
        return false;
    }
    return m_Client.Connect(ipAddress, port);
}

bool NetworkClient::ReceiveInitPacket(InitPacket& outInitData)
{
    PacketHeader header;
    if (!m_Client.ReceiveData(&header, sizeof(header))) return false;

    if (header.MagicNumber != PACKET_MAGIC_NUMBER || header.Type != PACKET_TYPE_INIT)
    {
        return false;
    }

    std::vector<BYTE> compressedData(header.CompressedSize);
    if (!m_Client.ReceiveData(compressedData.data(), header.CompressedSize)) return false;

    std::vector<BYTE> uncompressedData;
    if (!m_Compressor.Decompress(compressedData.data(), header.CompressedSize, uncompressedData, header.UncompressedSize))
    {
        return false;
    }

    if (uncompressedData.size() != sizeof(InitPacket)) return false;

    memcpy(&outInitData, uncompressedData.data(), sizeof(InitPacket));
    return true;
}

bool NetworkClient::ReceiveFramePacket(std::vector<BYTE>& outUncompressedData, FramePacketHeader& outHeader)
{
    PacketHeader header;
    if (!m_Client.ReceiveData(&header, sizeof(header))) return false;

    if (header.MagicNumber != PACKET_MAGIC_NUMBER || header.Type != PACKET_TYPE_FRAME)
    {
        return false;
    }

    std::vector<BYTE> compressedData(header.CompressedSize);
    if (!m_Client.ReceiveData(compressedData.data(), header.CompressedSize)) return false;

    if (!m_Compressor.Decompress(compressedData.data(), header.CompressedSize, outUncompressedData, header.UncompressedSize))
    {
        return false;
    }

    if (outUncompressedData.size() < sizeof(FramePacketHeader)) return false;

    memcpy(&outHeader, outUncompressedData.data(), sizeof(FramePacketHeader));
    return true;
}

bool NetworkClient::HasData()
{
    return m_Client.HasData();
}

void NetworkClient::Disconnect()
{
    m_Client.Disconnect();
}

bool NetworkClient::SendMouseInput(const MouseInputPacket& input)
{
    PacketHeader header;
    header.MagicNumber      = PACKET_MAGIC_NUMBER;
    header.Type             = PACKET_TYPE_MOUSE_INPUT;
    header.CompressedSize   = sizeof(MouseInputPacket);
    header.UncompressedSize = sizeof(MouseInputPacket);
    header.ProtocolVersion  = 1;
    header.Reserved[0]      = 0;
    header.Reserved[1]      = 0;
    header.Reserved[2]      = 0;

    if (!m_Client.SendData(&header, sizeof(header))) return false;
    return m_Client.SendData(&input, sizeof(input));
}

bool NetworkClient::ReceivePacketHeader(PacketHeader& outHeader)
{
    if (!m_Client.ReceiveData(&outHeader, sizeof(outHeader))) return false;
    return outHeader.MagicNumber == PACKET_MAGIC_NUMBER;
}

bool NetworkClient::ReceiveFramePacketBody(const PacketHeader& header, std::vector<BYTE>& outUncompressedData, FramePacketHeader& outFrameHeader)
{
    if (header.Type != PACKET_TYPE_FRAME) return false;

    std::vector<BYTE> compressedData(header.CompressedSize);
    if (!m_Client.ReceiveData(compressedData.data(), header.CompressedSize)) return false;

    if (!m_Compressor.Decompress(compressedData.data(), header.CompressedSize, outUncompressedData, header.UncompressedSize))
        return false;

    if (outUncompressedData.size() < sizeof(FramePacketHeader)) return false;

    memcpy(&outFrameHeader, outUncompressedData.data(), sizeof(FramePacketHeader));
    return true;
}

bool NetworkClient::ReceiveCursorShapeBody(const PacketHeader& header, CursorShapePacket& outPacket, std::vector<BYTE>& outShapeData)
{
    if (header.Type != PACKET_TYPE_CURSOR_SHAPE) return false;
    if (header.CompressedSize < sizeof(CursorShapePacket)) return false;

    if (!m_Client.ReceiveData(&outPacket, sizeof(CursorShapePacket))) return false;

    if (outPacket.ShapeBufferSize > 0)
    {
        outShapeData.resize(outPacket.ShapeBufferSize);
        if (!m_Client.ReceiveData(outShapeData.data(), outPacket.ShapeBufferSize)) return false;
    }
    else
    {
        outShapeData.clear();
    }

    return true;
}
