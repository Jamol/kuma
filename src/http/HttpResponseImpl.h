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

#ifndef __HttpResponseImpl_H__
#define __HttpResponseImpl_H__

#include "kmdefs.h"
#include "httpdefs.h"
#include "HttpParserImpl.h"
#include "TcpConnection.h"
#include "Uri.h"
#include "util/kmobject.h"
#include "util/DestroyDetector.h"
#include "compr/compr.h"

KUMA_NS_BEGIN

class HttpResponse::Impl : public KMObject
{
public:
    using DataCallback = HttpResponse::DataCallback;
    using EventCallback = HttpResponse::EventCallback;
    using HttpEventCallback = HttpResponse::HttpEventCallback;
    using EnumerateCallback = HttpParser::Impl::EnumerateCallback;
    
    Impl(std::string ver);
    virtual ~Impl();
    
    virtual KMError setSslFlags(uint32_t ssl_flags) { return KMError::NOT_SUPPORTED; }
    virtual KMError attachFd(SOCKET_FD fd, const KMBuffer *init_buf) { return KMError::NOT_SUPPORTED; }
    virtual KMError attachSocket(TcpSocket::Impl&& tcp, HttpParser::Impl&& parser, const KMBuffer *init_buf) { return KMError::NOT_SUPPORTED; }
    virtual KMError attachStream(H2Connection::Impl* conn, uint32_t stream_id) { return KMError::NOT_SUPPORTED; }
    virtual KMError addHeader(std::string name, std::string value) = 0;
    virtual KMError addHeader(std::string name, uint32_t value);
    KMError sendResponse(int status_code, const std::string& desc);
    int sendData(const void* data, size_t len);
    int sendData(const KMBuffer &buf);
    virtual void reset();
    virtual KMError close() = 0;
    
    virtual const std::string& getMethod() const = 0;
    virtual const std::string& getPath() const = 0;
    virtual const std::string& getQuery() const = 0;
    virtual const std::string& getVersion() const = 0;
    virtual const std::string& getParamValue(const std::string &name) const = 0;
    virtual const std::string& getHeaderValue(const std::string &name) const = 0;
    virtual void forEachHeader(const EnumerateCallback &cb) const = 0;
    
    void setDataCallback(DataCallback cb) { data_cb_ = std::move(cb); }
    void setWriteCallback(EventCallback cb) { write_cb_ = std::move(cb); }
    void setErrorCallback(EventCallback cb) { error_cb_ = std::move(cb); }
    void setHeaderCompleteCallback(HttpEventCallback cb) { header_cb_ = std::move(cb); }
    void setRequestCompleteCallback(HttpEventCallback cb) { request_cb_ = std::move(cb); }
    void setResponseCompleteCallback(HttpEventCallback cb) { response_cb_ = std::move(cb); }
    
protected:
    virtual KMError sendResponse(int status_code, const std::string& desc, const std::string& ver) = 0;
    virtual bool canSendBody() const = 0;
    virtual int sendBody(const void* data, size_t len) = 0;
    virtual int sendBody(const KMBuffer &buf) = 0;
    virtual void checkRequestHeaders() = 0;
    virtual void checkResponseHeaders() = 0;
    virtual const HttpHeader& getRequestHeader() const = 0;
    virtual HttpHeader& getResponseHeader() = 0;
    virtual bool isVersion2() { return true; }
    
    enum State {
        IDLE,
        RECVING_REQUEST,
        WAIT_FOR_RESPONSE,
        SENDING_HEADER,
        SENDING_BODY,
        COMPLETE,
        IN_ERROR,
        CLOSED
    };
    void setState(State state) { state_ = state; }
    State getState() const { return state_; }
    
    void onRequestHeaderComplete();
    void onRequestData(KMBuffer &buf);
    void onRequestComplete();
    void notifyComplete();
    void onSendReady();
    
protected:
    State                   state_ = State::IDLE;
    
    std::string             version_;
    
    DataCallback            data_cb_;
    EventCallback           write_cb_;
    EventCallback           error_cb_;
    HttpEventCallback       header_cb_;
    HttpEventCallback       request_cb_;
    HttpEventCallback       response_cb_;
    
    size_t                  raw_bytes_sent_ = 0;
    std::unique_ptr<Compressor> compressor_;
    std::unique_ptr<Decompressor> decompressor_;
    
    bool                    compression_finish_ = false;
    Compressor::DataBuffer  compression_buffer_;
};

KUMA_NS_END

#endif
