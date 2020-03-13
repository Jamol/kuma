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
#include "compr/compr_zlib.h"
#include "libkev/src/util/util.h"

using namespace kuma;
using namespace kuma::ws;


PMCE_Deflate::PMCE_Deflate()
{
    
}

PMCE_Deflate::~PMCE_Deflate()
{
    
}

KMError PMCE_Deflate::init()
{
    if (!negotiated_) {
        return KMError::INVALID_STATE;
    }
    {
        std::unique_ptr<ZLibCompressor> compr(new ZLibCompressor());
        auto ret = compr->init("raw-deflate", c_max_window_bits);
        if (ret != KMError::NOERR) {
            return ret;
        }
        if (!c_no_context_takeover) {
            compr->setFlushFlag(Z_SYNC_FLUSH);
        } else {
            compr->setFlushFlag(Z_FULL_FLUSH);
        }
        compressor_ = std::move(compr);
    }
    {
        std::unique_ptr<ZLibDecompressor> decompr(new ZLibDecompressor());
        auto ret = decompr->init("raw-deflate", d_max_window_bits);
        if (ret != KMError::NOERR) {
            return ret;
        }
        decompr->setFlushFlag(Z_SYNC_FLUSH);
        decompressor_ = std::move(decompr);
    }
    return KMError::NOERR;
}

KMError PMCE_Deflate::handleIncomingFrame(FrameHeader hdr, KMBuffer &payload)
{
    if (hdr.rsv1 && hdr.opcode < 8) {
        d_payload.clear();
        auto ret = decompressor_->decompress(payload, d_payload);
        if (ret != KMError::NOERR) {
            return ret;
        }
        if (hdr.fin) {
            uint8_t trailer[4] = {0x00, 0x00, 0xff, 0xff};
            ret = decompressor_->decompress(trailer, sizeof(trailer), d_payload);
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

KMError PMCE_Deflate::handleOutgoingFrame(FrameHeader hdr, KMBuffer &payload)
{
    c_payload.clear();
    auto ret = compressor_->compress(payload, c_payload);
    if (ret == KMError::NOERR && c_payload.size() >= 4) {
        if (hdr.fin) {
            c_payload.resize(c_payload.size()-4);
        }
        hdr.rsv1 = 1;
        KMBuffer o_payload(&c_payload[0], c_payload.size(), c_payload.size());
        return onOutgoingFrame(hdr, o_payload);
    } else { // else send as uncompressed
        return onOutgoingFrame(hdr, payload);
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
    negotiated_ = true;
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
    negotiated_ = true;
    return KMError::NOERR;
}

