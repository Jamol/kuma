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
#include "httputils.h"
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
    
    if (compression_enable_ && !req_encoding_type_.empty()) {
        auto *compr = new ZLibCompressor();
        compressor_.reset(compr);
        compr->setFlushFlag(Z_NO_FLUSH);
        if (compr->init(req_encoding_type_, 15) != KMError::NOERR) {
            compressor_.reset();
            auto &req_header = getRequestHeader();
            req_header.removeHeader(strContentEncoding);
            req_header.removeHeaderValue(strTransferEncoding, req_encoding_type_);
            KUMA_WARNXTRACE("sendRequest, failed to init compressor, type=" << req_encoding_type_);
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
    
    auto content_type = req_header.getHeader(strContentType);
    if (content_type.empty()) {
        content_type = "application/octet-stream";
        addHeader(strContentType, content_type);
    } else {
        // extract content type
        for_each_token(content_type, ';', [&content_type] (std::string &str) {
            content_type = str;
            return false;
        });
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
    
    req_encoding_type_.clear();
    auto encoding = req_header.getHeader(strContentEncoding);
    if (!encoding.empty()) {
        // caller do compression by itself
        compression_enable_ = false;
        KUMA_INFOXTRACE("checkRequestHeaders, request Content-Encoding=" << encoding);
        if (is_equal(encoding, "identity")) {
            req_header.removeHeader(strContentEncoding);
        }
    }
    
    if (req_header.hasContentLength() && req_header.getContentLength() == 0) {
        // no body data
        compression_enable_ = false;
    }
    
    if (compression_enable_) {
        if ((!is_equal(rsp_encoding_type_, "gzip") &&
             !is_equal(rsp_encoding_type_, "deflate")) ||
            isContentCompressed(content_type))
        {
            compression_enable_ = false;
        }
    }
    
    if (compression_enable_ && !req_encoding_type_.empty()) {
        bool is_content_encoding = true;
        if (is_content_encoding) {
            addHeader(strContentEncoding, req_encoding_type_);
            KUMA_INFOXTRACE("checkRequestHeaders, add Content-Encoding=" << req_encoding_type_);
            if (!req_header.isChunked()) {
                addHeader(strTransferEncoding, strChunked);
            }
        } else {
            addHeader(strTransferEncoding, req_encoding_type_ + ", chunked");
            KUMA_INFOXTRACE("checkRequestHeaders, add Transfer-Encoding=" << req_encoding_type_);
        }
        if (req_header.hasContentLength()) {
            // the Content-Length is no longer correct when compression is enabled
            req_header.removeHeader(strContentLength);
        }
    } else {
        compression_enable_ = false;
    }
}

void HttpRequest::Impl::checkResponseHeaders()
{
    auto &rsp_header = getResponseHeader();
    
    rsp_encoding_type_ = rsp_header.getHeader(strContentEncoding);
    if (rsp_encoding_type_.empty() && !isHttp2()) {
        auto encodings = rsp_header.getHeader(strTransferEncoding);
        for_each_token(encodings, ',', [this] (const std::string &str) {
            if (!is_equal(str, strChunked)) {
                rsp_encoding_type_ = str;
                return false;
            }
            return true;
        });
    }
    if (!rsp_encoding_type_.empty()) {
        KUMA_INFOXTRACE("checkResponseHeaders, Content-Encoding=" << rsp_encoding_type_);
    }
}

int HttpRequest::Impl::sendData(const void* data, size_t len)
{
    if (!canSendBody()) {
        return 0;
    }
    if (req_complete_) {
        return 0;
    }
    
    bool finish = false;
    auto send_len = len;
    auto const &req_header = getRequestHeader();
    if (req_header.hasContentLength()) {
        if (raw_bytes_sent_ + send_len >= req_header.getContentLength()) {
            send_len = req_header.getContentLength() - raw_bytes_sent_;
            finish = true;
        }
    } else if (req_header.isChunked()){
        finish = (!data || send_len == 0);
    }
    
    if (compressor_) {
        if (!compression_buffer_.empty()) {
            return 0;
        }
        
        Compressor::DataBuffer cbuf;
        if (data && send_len > 0) {
            auto compr_ret = compressor_->compress(data, send_len, cbuf);
            if (compr_ret != KMError::NOERR) {
                return -1;
            }
        }
        
        if (finish) {
            // body end, finish the compression
            auto compr_ret = compressor_->compress(nullptr, 0, cbuf);
            if (compr_ret != KMError::NOERR) {
                return -1;
            }
            req_complete_ = true;
        }
        
        raw_bytes_sent_ += send_len;
        if (!cbuf.empty()) {
            auto ret = sendBody(&cbuf[0], cbuf.size());
            if (ret < 0) {
                return ret;
            } else if (ret == 0) {
                compression_buffer_ = std::move(cbuf);
                compression_finish_ = finish;
                return (int)send_len;
            }
        }
        
        if (finish) {
            sendBody(nullptr, 0);
        }
        
        return (int)send_len;
    } else {
        auto ret =  sendBody(data, send_len);
        if (ret > 0) {
            raw_bytes_sent_ += ret;
        }
        if (finish && ret >= static_cast<int>(send_len)) {
            req_complete_ = true;
        }
        return ret;
    }
}

int HttpRequest::Impl::sendData(const KMBuffer &buf)
{
    if (!canSendBody()) {
        return 0;
    }
    if (req_complete_) {
        return 0;
    }
    
    auto buf_len = buf.chainLength();
    bool finish = false;
    auto send_len = buf_len;
    auto const &req_header = getRequestHeader();
    if (req_header.hasContentLength()) {
        if (raw_bytes_sent_ + send_len >= req_header.getContentLength()) {
            send_len = req_header.getContentLength() - raw_bytes_sent_;
            finish = true;
        }
    } else if (req_header.isChunked()){
        finish = send_len == 0;
    }
    
    if (compressor_) {
        if (!compression_buffer_.empty()) {
            return 0;
        }
        
        const KMBuffer *send_buf = &buf;
        if (send_len < buf_len) {
            send_buf = buf.subbuffer(0, send_len);
        }
        
        Compressor::DataBuffer cbuf;
        if (send_len > 0) {
            auto ret = compressor_->compress(*send_buf, cbuf);
            if (send_buf != &buf) {
                const_cast<KMBuffer*>(send_buf)->destroy();
            }
            if (ret != KMError::NOERR) {
                return -1;
            }
        }
        
        if (finish) {
            // body end, finish the compression
            auto compr_ret = compressor_->compress(nullptr, 0, cbuf);
            if (compr_ret != KMError::NOERR) {
                return -1;
            }
            req_complete_ = true;
        }
        
        raw_bytes_sent_ += send_len;
        if (!cbuf.empty()) {
            auto ret = sendBody(&cbuf[0], cbuf.size());
            if (ret < 0) {
                return ret;
            } else if (ret == 0) {
                compression_buffer_ = std::move(cbuf);
                compression_finish_ = finish;
                return (int)send_len;
            }
        }
        
        if (finish) {
            sendBody(nullptr, 0);
        }
        
        return (int)send_len;
    } else {
        const KMBuffer *send_buf = &buf;
        if (send_len < buf_len) {
            send_buf = buf.subbuffer(0, send_len);
        }
        auto ret = sendBody(*send_buf);
        if (send_buf != &buf) {
            const_cast<KMBuffer*>(send_buf)->destroy();
        }
        if (ret > 0) {
            raw_bytes_sent_ += ret;
        }
        if (finish && ret >= static_cast<int>(send_len)) {
            req_complete_ = true;
        }
        return ret;
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
    req_encoding_type_.clear();
    rsp_encoding_type_.clear();
    req_complete_ = false;
    raw_bytes_sent_ = 0;
    compressor_.reset();
    decompressor_.reset();
    compression_enable_ = true;
    compression_finish_ = false;
    compression_buffer_.clear();
    if (getState() == State::COMPLETE) {
        setState(State::WAIT_FOR_REUSE);
    }
}

void HttpRequest::Impl::onResponseHeaderComplete()
{
    checkResponseHeaders();
    
    if (!rsp_encoding_type_.empty()) {
        if (is_equal(rsp_encoding_type_, "gzip") ||
            is_equal(rsp_encoding_type_, "deflate"))
        {
            auto *decompr = new ZLibDecompressor();
            decompressor_.reset(decompr);
            decompr->setFlushFlag(Z_SYNC_FLUSH);
            if (decompr->init(rsp_encoding_type_, 15) != KMError::NOERR) {
                decompressor_.reset();
                KUMA_ERRXTRACE("onResponseHeaderComplete, failed to init decompressor, type=" << rsp_encoding_type_);
            }
        } else {
            KUMA_ERRXTRACE("onResponseHeaderComplete, unsupported encoding type: " << rsp_encoding_type_);
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
