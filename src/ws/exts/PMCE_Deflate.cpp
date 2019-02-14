/* Copyright (c) 2014-2019, Fengping Bao <jamol@live.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include "PMCE_Deflate.h"
#include "util/util.h"

using namespace kuma;
using namespace kuma::ws;


PMCE_Deflate::PMCE_Deflate()
{
    
}

PMCE_Deflate::~PMCE_Deflate()
{
    if (m_initialized) {
        deflateEnd(&c_stream);
        
        inflateEnd(&d_stream);
        
        m_initialized = false;
    }
}

KMError PMCE_Deflate::init()
{
    if (!m_negotiated) {
        return KMError::INVALID_STATE;
    }
    auto ret = deflateInit2(&c_stream,
                Z_DEFAULT_COMPRESSION,
                Z_DEFLATED,
                -1 * c_max_window_bits,
                c_memory_level,
                Z_DEFAULT_STRATEGY);
    if (ret != Z_OK) {
        return KMError::FAILED;
    }
    
    ret = inflateInit2(&d_stream, -1 * d_max_window_bits);
    if (ret != Z_OK) {
        return KMError::FAILED;
    }
    
    if (c_no_context_takeover) {
        c_flush = Z_FULL_FLUSH;
    }
    
    m_initialized = true;
    return KMError::NOERR;
}

KMError PMCE_Deflate::handleIncomingFrame(FrameHeader hdr, KMBuffer &payload)
{
    if (hdr.rsv1 && hdr.opcode < 8) {
        d_payload.clear();
        auto ret = decompress(payload, d_payload);
        if (ret != KMError::NOERR) {
            return ret;
        }
        if (hdr.fin) {
            uint8_t trailer[4] = {0x00, 0x00, 0xff, 0xff};
            ret = decompress(trailer, sizeof(trailer), d_payload);
            if (ret != KMError::NOERR) {
                return ret;
            }
        }
        hdr.rsv1 = 0;
        
        KMBuffer i_payload(&d_payload[0], d_payload.size(), d_payload.size());
        return onIncomingFrame(hdr, i_payload);
    } else {
        return onIncomingFrame(hdr, payload);
    }
}

KMError PMCE_Deflate::handleOutcomingFrame(FrameHeader hdr, KMBuffer &payload)
{
    c_payload.clear();
    auto ret = compress(payload, c_payload);
    if (ret == KMError::NOERR && c_payload.size() >= 4) {
        if (hdr.fin) {
            c_payload.resize(c_payload.size()-4);
        }
        hdr.rsv1 = 1;
        KMBuffer o_payload(&c_payload[0], c_payload.size(), c_payload.size());
        return onOutcomingFrame(hdr, o_payload);
    } else { // else send as uncompressed
        return onOutcomingFrame(hdr, payload);
    }
}

KMError PMCE_Deflate::getOffer(std::string &offer)
{
    c_max_window_bits = 15;
    offer = getExtensionName() + "; client_max_window_bits";
    return KMError::NOERR;
}

KMError PMCE_Deflate::negotiateAnswer(const std::string &answer)
{
    KeyValueList param_list;
    parseParameterList(answer, param_list);
    if (param_list.empty()) {
        return KMError::INVALID_PARAM;
    }
    if (param_list[0].first != getExtensionName()) {
        return KMError::INVALID_PARAM;
    }
    auto it = param_list.begin() + 1;
    for (; it != param_list.end(); ++it) {
        if (it->first == "client_max_window_bits") {
            if (!it->second.empty()) {
                auto client_max_window_bits = std::stoi(it->second);
                if (client_max_window_bits < 8 || client_max_window_bits > 15) {
                    return KMError::INVALID_PARAM;
                }
                c_max_window_bits = client_max_window_bits;
            }
        } else if (it->first == "server_max_window_bits") {
            if (it->second.empty()) {
                return KMError::INVALID_PARAM;
            }
            auto server_max_window_bits = std::stoi(it->second);
            if (server_max_window_bits < 8 || server_max_window_bits > 15) {
                return KMError::INVALID_PARAM;
            }
            d_max_window_bits = server_max_window_bits;
        } else if (it->first == "server_no_context_takeover") {
            
        } else if (it->first == "client_no_context_takeover") {
            c_no_context_takeover = true;
        } else {
            return KMError::INVALID_PARAM;
        }
    }
    m_negotiated = true;
    return KMError::NOERR;
}

KMError PMCE_Deflate::negotiateOffer(const std::string &offer, std::string &answer)
{
    KeyValueList param_list;
    parseParameterList(offer, param_list);
    if (param_list.empty()) {
        return KMError::INVALID_PARAM;
    }
    if (param_list[0].first != getExtensionName()) {
        return KMError::INVALID_PARAM;
    }
    answer = getExtensionName();
    auto it = param_list.begin() + 1;
    for (; it != param_list.end(); ++it) {
        if (it->first == "client_max_window_bits") {
            if (!it->second.empty()) {
                auto client_max_window_bits = std::stoi(it->second);
                if (client_max_window_bits < 8 || client_max_window_bits > 15) {
                    return KMError::INVALID_PARAM;
                }
                d_max_window_bits = client_max_window_bits;
            }
        } else if (it->first == "server_max_window_bits") {
            if (it->second.empty()) {
                return KMError::INVALID_PARAM;
            }
            auto server_max_window_bits = std::stoi(it->second);
            if (server_max_window_bits < 8 || server_max_window_bits > 15) {
                return KMError::INVALID_PARAM;
            }
            c_max_window_bits = server_max_window_bits;
            answer += "; server_max_window_bits=" + it->second;
        } else if (it->first == "server_no_context_takeover") {
            c_no_context_takeover = true;
            answer += "; server_no_context_takeover";
        } else if (it->first == "client_no_context_takeover") {
            // do nothing
        } else {
            return KMError::INVALID_PARAM;
        }
    }
    m_negotiated = true;
    return KMError::NOERR;
}

KMError PMCE_Deflate::compress(const uint8_t *ibuf, size_t ilen, DataBuffer &obuf)
{
    uint8_t cbuf[8192];
    c_stream.avail_in = static_cast<uInt>(ilen);
    c_stream.next_in = const_cast<Bytef *>(ibuf);
    
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

KMError PMCE_Deflate::decompress(const uint8_t *ibuf, size_t ilen, DataBuffer &obuf)
{
    uint8_t dbuf[8192];
    d_stream.avail_in = static_cast<uInt>(ilen);
    d_stream.next_in = const_cast<Bytef *>(ibuf);
    
    do {
        d_stream.avail_out = sizeof(dbuf);
        d_stream.next_out = dbuf;
        auto ret = inflate(&d_stream, d_flush);
        if (ret != Z_OK) {
            return KMError::FAILED;
        }
        auto dlen = sizeof(dbuf) - d_stream.avail_out;
        obuf.insert(obuf.end(), dbuf, dbuf + dlen);
    } while (d_stream.avail_out == 0);
    
    return KMError::NOERR;
}

KMError PMCE_Deflate::compress(const KMBuffer &ibuf, DataBuffer &obuf)
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

KMError PMCE_Deflate::decompress(const KMBuffer &ibuf, DataBuffer &obuf)
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

