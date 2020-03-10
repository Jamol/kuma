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

#include "compr_zlib.h"
#include "libkev/src/util/util.h"

using namespace kuma;

ZLibCompressor::ZLibCompressor()
{
    
}

ZLibCompressor::~ZLibCompressor()
{
    if (initizlized_) {
        deflateEnd(&c_stream);
        initizlized_ = false;
    }
}

KMError ZLibCompressor::init(const std::string &type, int max_window_bits)
{
    if (max_window_bits > 15 || max_window_bits < 8) {
        return KMError::INVALID_PARAM;
    }
    if (initizlized_) {
        deflateEnd(&c_stream);
        initizlized_ = false;
    }
    if (kev::is_equal(type, "gzip")) {
        c_max_window_bits = max_window_bits + 16;
    } else if (kev::is_equal(type, "raw-deflate")) {
        c_max_window_bits = -1 * max_window_bits;
    } else if (kev::is_equal(type, "deflate")) {
        c_max_window_bits = max_window_bits;
    } else {
        return KMError::INVALID_PARAM;
    }
    auto ret = deflateInit2(&c_stream,
                            Z_DEFAULT_COMPRESSION,
                            Z_DEFLATED,
                            c_max_window_bits,
                            c_memory_level,
                            Z_DEFAULT_STRATEGY);
    if (ret != Z_OK) {
        return KMError::FAILED;
    }
    initizlized_ = true;
    return KMError::NOERR;
}

void ZLibCompressor::setFlushFlag(int flush)
{
    c_flush = flush;
}

KMError ZLibCompressor::compress2(const void *ibuf, size_t ilen, DataBuffer &obuf)
{
    uint8_t cbuf[4096];
    c_stream.avail_in = static_cast<uInt>(ilen);
    c_stream.next_in = const_cast<Bytef *>((const Bytef*)ibuf);
    
    do {
        c_stream.avail_out = sizeof(cbuf);
        c_stream.next_out = cbuf;
        auto ret = deflate(&c_stream, c_flush);
        if (ret < 0) {
            return KMError::FAILED;
        }
        auto clen = sizeof(cbuf) - c_stream.avail_out;
        obuf.insert(obuf.end(), cbuf, cbuf + clen);
    } while (c_stream.avail_out == 0);
    
    return KMError::NOERR;
}

KMError ZLibCompressor::compress(const void *ibuf, size_t ilen, DataBuffer &obuf)
{
    bool finish = !ibuf || ilen == 0;
    
    size_t ioffset {0};
    uint8_t cbuf[300];
    
    int flush = c_flush;
    auto iavail = ilen - ioffset;
    do {
        if (iavail == 0 && finish) {
            flush = Z_FINISH;
        }
        
        c_stream.avail_in = static_cast<uInt>(iavail);
        c_stream.next_in = const_cast<Bytef *>((const Bytef*)ibuf + ioffset);
        
        do {
            c_stream.avail_out = sizeof(cbuf);
            c_stream.next_out = cbuf;
            auto ret = deflate(&c_stream, flush);
            if (ret < 0) {
                return KMError::FAILED;
            }
            auto clen = sizeof(cbuf) - c_stream.avail_out;
            obuf.insert(obuf.end(), cbuf, cbuf + clen);
        } while (c_stream.avail_out == 0);
        
        ioffset += iavail - c_stream.avail_in;
        iavail = ilen - ioffset;
    } while (iavail > 0);
    
    return KMError::NOERR;
}

KMError ZLibCompressor::compress(const KMBuffer &ibuf, DataBuffer &obuf)
{
    for (auto it = ibuf.begin(); it != ibuf.end(); ++it) {
        if (it->length() > 0) {
            auto *cbuf = static_cast<uint8_t *>(it->readPtr());
            auto ret = compress(cbuf, it->length(), obuf);
            if (ret != KMError::NOERR) {
                return ret;
            }
        }
    }
    
    return KMError::NOERR;
}

////////////////////////////////////////////////////////////////////////////////////
ZLibDecompressor::ZLibDecompressor()
{
    
}

ZLibDecompressor::~ZLibDecompressor()
{
    if (initizlized_) {
        inflateEnd(&d_stream);
        initizlized_ = false;
    }
}

KMError ZLibDecompressor::init(const std::string &type, int max_window_bits)
{
    if (max_window_bits > 15 || max_window_bits < 8) {
        return KMError::INVALID_PARAM;
    }
    if (initizlized_) {
        inflateEnd(&d_stream);
        initizlized_ = false;
    }
    if (kev::is_equal(type, "gzip")) {
        d_max_window_bits = max_window_bits + 16;
    } else if (kev::is_equal(type, "raw-deflate")) {
        d_max_window_bits = -1 * max_window_bits;
    } else if (kev::is_equal(type, "deflate")) {
        d_max_window_bits = max_window_bits;
    } else {
        return KMError::INVALID_PARAM;
    }
    auto ret = inflateInit2(&d_stream, d_max_window_bits);
    if (ret != Z_OK) {
        return KMError::FAILED;
    }
    initizlized_ = true;
    return KMError::NOERR;
}

void ZLibDecompressor::setFlushFlag(int flush)
{
    d_flush = flush;
}

KMError ZLibDecompressor::decompress(const void *ibuf, size_t ilen, DataBuffer &obuf)
{
    uint8_t dbuf[300];
    d_stream.avail_in = static_cast<uInt>(ilen);
    d_stream.next_in = const_cast<Bytef *>((const Bytef*)ibuf);
    
    do {
        d_stream.avail_out = sizeof(dbuf);
        d_stream.next_out = dbuf;
        auto ret = inflate(&d_stream, d_flush);
        if (ret != Z_OK && ret != Z_STREAM_END) {
            return KMError::FAILED;
        }
        auto dlen = sizeof(dbuf) - d_stream.avail_out;
        obuf.insert(obuf.end(), dbuf, dbuf + dlen);
    } while (d_stream.avail_out == 0);
    
    return KMError::NOERR;
}

KMError ZLibDecompressor::decompress(const KMBuffer &ibuf, DataBuffer &obuf)
{
    for (auto it = ibuf.begin(); it != ibuf.end(); ++it) {
        if (it->length() > 0) {
            auto *dbuf = static_cast<uint8_t *>(it->readPtr());
            auto ret = decompress(dbuf, it->length(), obuf);
            if (ret != KMError::NOERR) {
                return ret;
            }
        }
    }
    
    return KMError::NOERR;
}
