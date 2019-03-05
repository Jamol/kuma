/* Copyright (c) 2014 - 2019, Fengping Bao <jamol@live.com>
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#pragma once

#include "compr.h"
#include "zlib/zlib.h"

#include <string>

KUMA_NS_BEGIN

class ZLibCompressor : public Compressor
{
public:
    ZLibCompressor();
    virtual ~ZLibCompressor();
    
    KMError init(const std::string &type, int max_window_bits);
    void setFlushFlag(int flush);
    KMError compress(const void *ibuf, size_t ilen, DataBuffer &obuf) override;
    KMError compress(const KMBuffer &ibuf, DataBuffer &obuf) override;
    
protected:
    KMError compress2(const void *ibuf, size_t ilen, DataBuffer &obuf);
    
protected:
    bool        initizlized_ = false;
    z_stream    c_stream {0};
    int         c_flush = Z_SYNC_FLUSH;
    int         c_max_window_bits = 15;
    int         c_memory_level = 8;
};

class ZLibDecompressor : public Decompressor
{
public:
    ZLibDecompressor();
    virtual ~ZLibDecompressor();
    
    KMError init(const std::string &type, int max_window_bits);
    void setFlushFlag(int flush);
    KMError decompress(const void *ibuf, size_t ilen, DataBuffer &obuf) override;
    KMError decompress(const KMBuffer &ibuf, DataBuffer &obuf) override;
    
protected:
    bool        initizlized_ = false;
    z_stream    d_stream {0};
    int         d_flush = Z_SYNC_FLUSH;
    int         d_max_window_bits = 15;
};

KUMA_NS_END
