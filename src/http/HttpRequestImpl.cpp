/* Copyright (c) 2016, Fengping Bao <jamol@live.com>
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

#include "HttpRequestImpl.h"
#include "util/kmtrace.h"
#include "util/util.h"
#include "compr/compr_zlib.h"

#include <sstream>
#include <iterator>

using namespace kuma;

//////////////////////////////////////////////////////////////////////////
//
HttpRequest::Impl::Impl(std::string ver)
: version_(std::move(ver))
{
    
}

KMError HttpRequest::Impl::addHeader(std::string name, uint32_t value)
{
    return addHeader(std::move(name), std::to_string(value));
}

KMError HttpRequest::Impl::sendRequest(std::string method, std::string url)
{
    if (getState() == State::COMPLETE) {
        reset(); // reuse case
    }
    if (getState() != State::IDLE && getState() != State::WAIT_FOR_REUSE) {
        return KMError::INVALID_STATE;
    }
    method_ = std::move(method);
    url_ = std::move(url);
    if(!uri_.parse(url_)) {
        return KMError::INVALID_PARAM;
    }
    checkRequestHeaders();
    
    auto &req_header = getRequestHeader();
    auto req_encoding = req_header.getEncodingType();
    if (!req_encoding.empty()) {
        if (is_equal(req_encoding, "gzip") || is_equal(req_encoding, "deflate")) {
            auto *compr = new ZLibCompressor();
            compressor_.reset(compr);
            compr->setFlushFlag(Z_NO_FLUSH);
            if (compr->init(req_encoding, 15) != KMError::NOERR) {
                compressor_.reset();
                req_header.removeHeader(strContentEncoding);
                req_header.removeHeaderValue(strTransferEncoding, req_encoding);
                KUMA_WARNXTRACE("sendRequest, failed to init compressor, type=" << req_encoding);
            }
        } else {
            req_header.removeHeader(strContentEncoding);
            req_header.removeHeaderValue(strTransferEncoding, req_encoding);
            KUMA_WARNXTRACE("sendRequest, unsupport encoding type: " << req_encoding);
        }
    }
    
    return sendRequest();
}

void HttpRequest::Impl::checkRequestHeaders()
{
    auto &req_header = getRequestHeader();
    if (!req_header.hasHeader("Accept")) {
        addHeader("Accept", "*/*");
    }
    if (!req_header.hasHeader(strContentType)) {
        addHeader(strContentType, "application/octet-stream");
    }
    if (!req_header.hasHeader(strUserAgent)) {
        addHeader(strUserAgent, UserAgent);
    }
    if (!isHttp2()) {
        addHeader(strHost, uri_.getHost());
    }
    if (!req_header.hasHeader(strCacheControl)) {
        addHeader(strCacheControl, "no-cache");
    }
    if (!req_header.hasHeader("Pragma")) {
        addHeader("Pragma", "no-cache");
    }
    if (!req_header.hasHeader(strAcceptEncoding)) {
        addHeader(strAcceptEncoding, "gzip, deflate");
    }
    /*if (!isHttp2() && !req_header.hasHeader("TE")) {
     addHeader("TE", "gzip, deflate");
     }*/
    if (is_equal(method_, "POST") &&
        !req_header.hasHeader(strTransferEncoding) &&
        !req_header.hasHeader(strContentLength))
    {
        addHeader(strContentLength, "0");
    }
}

int HttpRequest::Impl::sendData(const void* data, size_t len)
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
        
        auto &req_header = getRequestHeader();
        bool finish = (!data || len == 0) ||
            (req_header.hasContentLength() && raw_bytes_sent_ >= req_header.getContentLength());
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

int HttpRequest::Impl::sendData(const KMBuffer &buf)
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
        
        auto &req_header = getRequestHeader();
        bool finish = buf_len == 0 ||
            (req_header.hasContentLength() && raw_bytes_sent_ >= req_header.getContentLength());
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

std::string HttpRequest::Impl::getCacheKey()
{
    std::string cache_key = uri_.getHost() + uri_.getPath();
    if (!uri_.getQuery().empty()) {
        cache_key += "?";
        cache_key += uri_.getQuery();
    }
    return cache_key;
}

void HttpRequest::Impl::reset()
{
    raw_bytes_sent_ = 0;
    compressor_.reset();
    decompressor_.reset();
}

void HttpRequest::Impl::onResponseHeaderComplete()
{
    checkResponseHeaders();
    auto rsp_encoding = getResponseHeader().getEncodingType();
    if (!rsp_encoding.empty()) {
        if (is_equal(rsp_encoding, "gzip") || is_equal(rsp_encoding, "deflate")) {
            auto *decompr = new ZLibDecompressor();
            decompressor_.reset(decompr);
            decompr->setFlushFlag(Z_SYNC_FLUSH);
            if (decompr->init(rsp_encoding, 15) != KMError::NOERR) {
                decompressor_.reset();
                KUMA_ERRXTRACE("onResponseHeaderComplete, failed to init decompressor, type=" << rsp_encoding);
            }
        } else {
            KUMA_ERRXTRACE("onResponseHeaderComplete, unsupport encoding type: " << rsp_encoding);
        }
    }
    if(header_cb_) header_cb_();
}

void HttpRequest::Impl::onResponseData(KMBuffer &buf)
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

void HttpRequest::Impl::onResponseComplete()
{
    setState(State::COMPLETE);
    if(response_cb_) response_cb_();
}

void HttpRequest::Impl::onSendReady()
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
