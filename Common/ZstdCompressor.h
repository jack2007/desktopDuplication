// THIS CODE AND INFORMATION IS PROVIDED "AS IS" WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING BUT NOT LIMITED TO
// THE IMPLIED WARRANTIES OF MERCHANTABILITY AND/OR FITNESS FOR A
// PARTICULAR PURPOSE.
//
// Copyright (c) Microsoft Corporation. All rights reserved

#ifndef _ZSTDCOMPRESSOR_H_
#define _ZSTDCOMPRESSOR_H_

#include <windows.h>
#include <vector>

// Forward declaration for ZSTD context
typedef struct ZSTD_CCtx_s ZSTD_CCtx;
typedef struct ZSTD_DCtx_s ZSTD_DCtx;

class ZstdCompressor
{
public:
    ZstdCompressor();
    ~ZstdCompressor();

    bool Initialize(unsigned int compressLevel);
    bool Compress(const void* src, size_t srcSize, std::vector<BYTE>& dst);
    bool Decompress(const void* src, size_t srcSize, std::vector<BYTE>& dst, size_t uncompressedSize);

private:
    ZSTD_CCtx* m_cctx;
    ZSTD_DCtx* m_dctx;
	unsigned int m_compressLevel;
};

#endif
