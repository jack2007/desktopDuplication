// THIS CODE AND INFORMATION IS PROVIDED "AS IS" WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING BUT NOT LIMITED TO
// THE IMPLIED WARRANTIES OF MERCHANTABILITY AND/OR FITNESS FOR A
// PARTICULAR PURPOSE.
//
// Copyright (c) Microsoft Corporation. All rights reserved

#include "ZstdCompressor.h"
#include <zstd.h>

ZstdCompressor::ZstdCompressor() : m_cctx(nullptr), m_dctx(nullptr), m_compressLevel(1)
{
}

ZstdCompressor::~ZstdCompressor()
{
    if (m_cctx) ZSTD_freeCCtx(m_cctx);
    if (m_dctx) ZSTD_freeDCtx(m_dctx);
}

bool ZstdCompressor::Initialize(unsigned int compressLevel)
{
    m_cctx = ZSTD_createCCtx();
    m_dctx = ZSTD_createDCtx();
	m_compressLevel = compressLevel;
    return m_cctx != nullptr && m_dctx != nullptr;
}

bool ZstdCompressor::Compress(const void* src, size_t srcSize, std::vector<BYTE>& dst)
{
    if (!m_cctx || !src || srcSize == 0) return false;

    size_t const cBuffSize = ZSTD_compressBound(srcSize);
    dst.resize(cBuffSize);

    size_t const cSize = ZSTD_compressCCtx(m_cctx, dst.data(), cBuffSize, src, srcSize, m_compressLevel); // Compression level 1 for speed
    if (ZSTD_isError(cSize))
    {
        return false;
    }

    dst.resize(cSize);
    return true;
}

bool ZstdCompressor::Decompress(const void* src, size_t srcSize, std::vector<BYTE>& dst, size_t uncompressedSize)
{
    if (!m_dctx || !src || srcSize == 0 || uncompressedSize == 0) return false;

    dst.resize(uncompressedSize);

    size_t const dSize = ZSTD_decompressDCtx(m_dctx, dst.data(), uncompressedSize, src, srcSize);
    if (ZSTD_isError(dSize) || dSize != uncompressedSize)
    {
        return false;
    }

    return true;
}
