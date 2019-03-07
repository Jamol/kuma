/* Copyright (c) 2014, Fengping Bao <jamol@live.com>
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

#include "HttpResponseImpl.h"
#include "EventLoopImpl.h"
#include "util/kmtrace.h"
#include "compr/compr_zlib.h"

#include <iterator>

using namespace kuma;

//////////////////////////////////////////////////////////////////////////
HttpResponse::Impl::Impl(std::string ver)
: version_(std::move(ver))
{
    
}

HttpResponse::Impl::~Impl()
{
    
}

KMError HttpResponse::Impl::addHeader(std::string name, uint32_t value)
{
    return addHeader(std::move(name), std::to_string(value));
}

KMError HttpResponse::Impl::sendResponse(int status_code, const std::string& desc)
{
    if (getState() != State::WAIT_FOR_RESPONSE) {
        return KMError::INVALID_STATE;
    }
    checkResponseHeaders();
    
    auto &rsp_header = getResponseHeader();
    auto rsp_encoding = rsp_header.getEncodingType();
    if (!rsp_encoding.empty()) {
        if (is_equal(rsp_encoding, "gzip") || is_equal(rsp_encoding, "deflate")) {
            auto *compr = new ZLibCompressor();
            compressor_.reset(compr);
            compr->setFlushFlag(Z_NO_FLUSH);
            if (compr->init(rsp_encoding, 15) != KMError::NOERR) {
                compressor_.reset();
                rsp_header.removeHeader(strContentEncoding);
                rsp_header.removeHeaderValue(strTransferEncoding, rsp_encoding);
                KUMA_WARNXTRACE("sendResponse, failed to init compressor, type=" << rsp_encoding);
            }
        } else {
            rsp_header.removeHeader(strContentEncoding);
            rsp_header.removeHeaderValue(strTransferEncoding, rsp_encoding);
            KUMA_WARNXTRACE("sendResponse, unsupport encoding type: " << rsp_encoding);
        }
    }
    
    return sendResponse(status_code, desc, version_);
}

int HttpResponse::Impl::sendData(const void* data, size_t len)
{
    if (!canSendBody()) {
        return 0;
    }
    
    if (compressor_) {
        if (!compression_buffer_.empty()) {
            return 0;
        }
        
        Compressor::DataBuffer cbuf;
        if (data && len > 0) {
            auto compr_ret = compressor_->compress(data, len, cbuf);
            if (compr_ret != KMError::NOERR) {
                return -1;
            }
        }
        raw_bytes_sent_ += len;
        
        auto &rsp_header = getResponseHeader();
        bool finish = (!data || len == 0) ||
            (rsp_header.hasContentLength() &&
             raw_bytes_sent_ >= rsp_header.getContentLength());
        if (finish) {
            // body end, finish the compression
            auto compr_ret = compressor_->compress(nullptr, 0, cbuf);
            if (compr_ret != KMError::NOERR) {
                return -1;
            }
        }
        
        if (!cbuf.empty()) {
            auto ret = sendBody(&cbuf[0], cbuf.size());
            if (ret < 0) {
                return ret;
            } else if (ret == 0) {
                compression_buffer_ = std::move(cbuf);
                compression_finish_ = finish;
                return (int)len;
            }
        }
        
        if (finish) {
            sendBody(nullptr, 0);
        }
        
        return (int)len;
    } else {
        return sendBody(data, len);
    }
}

int HttpResponse::Impl::sendData(const KMBuffer &buf)
{
    if (!canSendBody()) {
        return 0;
    }
    
    if (compressor_) {
        if (!compression_buffer_.empty()) {
            return 0;
        }
        
        auto buf_len = buf.chainLength();
        
        Compressor::DataBuffer cbuf;
        if (buf_len > 0) {
            auto ret = compressor_->compress(buf, cbuf);
            if (ret != KMError::NOERR) {
                return -1;
            }
        }
        raw_bytes_sent_ += buf_len;
        
        auto &rsp_header = getResponseHeader();
        bool finish = buf_len == 0 ||
            (rsp_header.hasContentLength() && raw_bytes_sent_ >= rsp_header.getContentLength());
        if (finish) {
            // body end, finish the compression
            auto compr_ret = compressor_->compress(nullptr, 0, cbuf);
            if (compr_ret != KMError::NOERR) {
                return -1;
            }
        }
        
        if (!cbuf.empty()) {
            auto ret = sendBody(&cbuf[0], cbuf.size());
            if (ret < 0) {
                return ret;
            } else if (ret == 0) {
                compression_buffer_ = std::move(cbuf);
                compression_finish_ = finish;
                return (int)buf_len;
            }
        }
        
        if (finish) {
            sendBody(nullptr, 0);
        }
        
        return (int)buf_len;
    } else {
        return sendBody(buf);
    }
}

void HttpResponse::Impl::reset()
{
    raw_bytes_sent_ = 0;
    compressor_.reset();
    decompressor_.reset();
}

void HttpResponse::Impl::onRequestHeaderComplete()
{
    checkRequestHeaders();
    auto req_encoding = getRequestHeader().getEncodingType();
    if (!req_encoding.empty()) {
        if (is_equal(req_encoding, "gzip") || is_equal(req_encoding, "deflate")) {
            auto *decompr = new ZLibDecompressor();
            decompressor_.reset(decompr);
            decompr->setFlushFlag(Z_SYNC_FLUSH);
            if (decompr->init(req_encoding, 15) != KMError::NOERR) {
                decompressor_.reset();
                KUMA_ERRXTRACE("onRequestHeaderComplete, failed to init decompressor, type=" << req_encoding);
            }
        } else {
            KUMA_ERRXTRACE("onRequestHeaderComplete, unsupport encoding type: " << req_encoding);
        }
    }
    if(header_cb_) header_cb_();
}

void HttpResponse::Impl::onRequestData(KMBuffer &buf)
{
    if(data_cb_) {
        if (decompressor_) {
            Decompressor::DataBuffer dbuf;
            auto decompr_ret = decompressor_->decompress(buf, dbuf);
            if (decompr_ret != KMError::NOERR) {
                return ;
            }
            if (!dbuf.empty()) {
                KMBuffer buf(&dbuf[0], dbuf.size(), dbuf.size());
                data_cb_(buf);
            }
        } else {
            data_cb_(buf);
        }
    }
}

void HttpResponse::Impl::onRequestComplete()
{
    setState(State::WAIT_FOR_RESPONSE);
    if(request_cb_) request_cb_();
}

void HttpResponse::Impl::notifyComplete()
{
    KUMA_INFOXTRACE("notifyComplete, raw bytes sent: " << raw_bytes_sent_);
    if(response_cb_) response_cb_();
}

void HttpResponse::Impl::onSendReady()
{
    if (!compression_buffer_.empty()) {
        auto ret = sendBody(&compression_buffer_[0], compression_buffer_.size());
        if (ret <= 0) {
            return ;
        }
        if (compression_finish_) {
            sendBody(nullptr, 0);
        }
        compression_buffer_.clear();
    }
    if (write_cb_) write_cb_(KMError::NOERR);
}

