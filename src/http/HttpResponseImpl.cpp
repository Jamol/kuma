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
#include "httputils.h"
#include "libkev/src/util/kmtrace.h"
#include "libkev/src/util/util.h"
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
    
    if (compression_enable_ && !rsp_encoding_type_.empty()) {
        auto *compr = new ZLibCompressor();
        compressor_.reset(compr);
        compr->setFlushFlag(Z_NO_FLUSH);
        if (compr->init(rsp_encoding_type_, 15) != KMError::NOERR) {
            compressor_.reset();
            auto &rsp_header = getResponseHeader();
            rsp_header.removeHeader(strContentEncoding);
            rsp_header.removeHeaderValue(strTransferEncoding, rsp_encoding_type_);
            KM_WARNXTRACE("sendResponse, failed to init compressor, type=" << rsp_encoding_type_);
        }
    }
    
    setState(State::SENDING_RESPONSE);
    return sendResponse(status_code, desc, version_);
}

void HttpResponse::Impl::checkRequestHeaders()
{
    rsp_encoding_type_.clear();
    is_content_encoding_ = true;
    auto &req_header = getRequestHeader();
    auto encodings = req_header.getHeader(strAcceptEncoding);
    if (encodings.empty() && !isHttp2()) {
        encodings = req_header.getHeader("TE");
        is_content_encoding_ = !encodings.empty();
    }
    kev::for_each_token(encodings, ',', [this] (const std::string &str) {
        if (kev::is_equal(str, "gzip")) {
            rsp_encoding_type_ = "gzip";
            return false;
        } else if (kev::is_equal(str, "deflate")) {
            rsp_encoding_type_ = "deflate";
            return false;
        }
        return true;
    });
    
    req_encoding_type_ = req_header.getHeader(strContentEncoding);
    if (req_encoding_type_.empty() && !isHttp2()) {
        encodings = req_header.getHeader(strTransferEncoding);
        kev::for_each_token(encodings, ',', [this] (const std::string &str) {
            if (!kev::is_equal(str, strChunked)) {
                req_encoding_type_ = str;
                return false;
            }
            return true;
        });
    }
    if (!req_encoding_type_.empty()) {
        KM_INFOXTRACE("checkRequestHeaders, Content-Encoding=" << req_encoding_type_);
    }
}

void HttpResponse::Impl::checkResponseHeaders()
{
    auto &rsp_header = getResponseHeader();
    
    auto content_type = rsp_header.getHeader(strContentType);
    if (content_type.empty()) {
        content_type = "application/octet-stream";
        addHeader(strContentType, content_type);
    } else {
        // extract content type
        kev::for_each_token(content_type, ';', [&content_type] (std::string &str) {
            content_type = str;
            return false;
        });
    }
    
    auto encoding = rsp_header.getHeader(strContentEncoding);
    if (!encoding.empty()) {
        // caller do compression by itself
        compression_enable_ = false;
        KM_INFOXTRACE("checkResponseHeaders, response Content-Encoding=" << encoding);
        if (kev::is_equal(encoding, "identity")) {
            rsp_header.removeHeader(strContentEncoding);
        }
    }
    
    if (rsp_header.hasContentLength() && rsp_header.getContentLength() == 0) {
        // no body data
        compression_enable_ = false;
    }
    
    if (compression_enable_) {
        if ((!kev::is_equal(rsp_encoding_type_, "gzip") &&
             !kev::is_equal(rsp_encoding_type_, "deflate")) ||
            isContentCompressed(content_type))
        {
            compression_enable_ = false;
        }
    }
    
    if (compression_enable_ && !rsp_encoding_type_.empty()) {
        if (is_content_encoding_) {
            addHeader(strContentEncoding, rsp_encoding_type_);
            KM_INFOXTRACE("checkResponseHeaders, add Content-Encoding=" << rsp_encoding_type_);
            if (!rsp_header.isChunked()) {
                addHeader(strTransferEncoding, strChunked);
            }
        } else {
            addHeader(strTransferEncoding, rsp_encoding_type_ + ", chunked");
            KM_INFOXTRACE("checkResponseHeaders, add Transfer-Encoding=" << rsp_encoding_type_);
        }
        if (rsp_header.hasContentLength()) {
            // the Content-Length is no longer correct when compression is enabled
            rsp_header.removeHeader(strContentLength);
        }
    } else {
        compression_enable_ = false;
    }
}

int HttpResponse::Impl::sendData(const void* data, size_t len)
{
    if (!canSendBody()) {
        return 0;
    }
    if (rsp_complete_) {
        return 0;
    }
    
    bool finish = false;
    auto send_len = len;
    auto const &rsp_header = getResponseHeader();
    if (rsp_header.hasContentLength()) {
        if (raw_bytes_sent_ + send_len >= rsp_header.getContentLength()) {
            send_len = rsp_header.getContentLength() - raw_bytes_sent_;
            finish = true;
        }
    } else if (rsp_header.isChunked()){
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
            rsp_complete_ = true;
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
            rsp_complete_ = true;
        }
        return ret;
    }
}

int HttpResponse::Impl::sendData(const KMBuffer &buf)
{
    if (!canSendBody()) {
        return 0;
    }
    if (rsp_complete_) {
        return 0;
    }
    
    auto buf_len = buf.chainLength();
    bool finish = false;
    auto send_len = buf_len;
    auto const &rsp_header = getResponseHeader();
    if (rsp_header.hasContentLength()) {
        if (raw_bytes_sent_ + send_len >= rsp_header.getContentLength()) {
            send_len = rsp_header.getContentLength() - raw_bytes_sent_;
            finish = true;
        }
    } else if (rsp_header.isChunked()){
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
            rsp_complete_ = true;
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
            rsp_complete_ = true;
        }
        return ret;
    }
}

void HttpResponse::Impl::reset()
{
    req_encoding_type_.clear();
    rsp_encoding_type_.clear();
    rsp_complete_ = false;
    raw_bytes_sent_ = 0;
    compressor_.reset();
    decompressor_.reset();
    compression_enable_ = true;
    compression_finish_ = false;
    compression_buffer_.clear();
    setState(State::RECVING_REQUEST);
}

void HttpResponse::Impl::onRequestHeaderComplete()
{
    checkRequestHeaders();
    
    if (!req_encoding_type_.empty()) {
        if (kev::is_equal(req_encoding_type_, "gzip") ||
            kev::is_equal(req_encoding_type_, "deflate"))
        {
            auto *decompr = new ZLibDecompressor();
            decompressor_.reset(decompr);
            decompr->setFlushFlag(Z_SYNC_FLUSH);
            if (decompr->init(req_encoding_type_, 15) != KMError::NOERR) {
                decompressor_.reset();
                KM_ERRXTRACE("onRequestHeaderComplete, failed to init decompressor, type=" << req_encoding_type_);
            }
        } else {
            KM_ERRXTRACE("onRequestHeaderComplete, unsupported encoding type: " << req_encoding_type_);
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
    KM_INFOXTRACE("notifyComplete, raw bytes sent: " << raw_bytes_sent_);
    setState(State::COMPLETE);
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

