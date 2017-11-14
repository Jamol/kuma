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

#ifndef __HttpRequestImpl_H__
#define __HttpRequestImpl_H__

#include "kmdefs.h"
#include "kmapi.h"
#include "httpdefs.h"
#include "Uri.h"
#include "HttpParserImpl.h"
#include <map>

KUMA_NS_BEGIN

class HttpRequest::Impl
{
public:
    using DataCallback = HttpRequest::DataCallback;
    using EventCallback = HttpRequest::EventCallback;
    using HttpEventCallback = HttpRequest::HttpEventCallback;
    using EnumrateCallback = HttpParser::Impl::EnumrateCallback;
    
    Impl(std::string ver);
    virtual ~Impl() = default;
    
    virtual KMError setSslFlags(uint32_t ssl_flags) = 0;
    virtual void addHeader(std::string name, std::string value) = 0;
    virtual void addHeader(std::string name, uint32_t value);
    KMError sendRequest(std::string method, std::string url);
    virtual int sendData(const void* data, size_t len) = 0;
    virtual int sendData(const KMBuffer &buf) = 0;
    virtual void reset();
    virtual KMError close() = 0;
    
    virtual int getStatusCode() const = 0;
    virtual const std::string& getVersion() const = 0;
    virtual const std::string& getHeaderValue(std::string name) const = 0;
    virtual void forEachHeader(EnumrateCallback cb) = 0;
    
    std::string getCacheKey();
    
    void setDataCallback(DataCallback cb) { data_cb_ = std::move(cb); }
    void setWriteCallback(EventCallback cb) { write_cb_ = std::move(cb); }
    void setErrorCallback(EventCallback cb) { error_cb_ = std::move(cb); }
    void setHeaderCompleteCallback(HttpEventCallback cb) { header_cb_ = std::move(cb); }
    void setResponseCompleteCallback(HttpEventCallback cb) { response_cb_ = std::move(cb); }
    
protected:
    virtual KMError sendRequest() = 0;
    virtual void checkHeaders() = 0;
    virtual bool isVersion2() { return true; }
    
    enum class State {
        IDLE,
        CONNECTING,
        SENDING_HEADER,
        SENDING_BODY,
        RECVING_RESPONSE,
        COMPLETE,
        WAIT_FOR_REUSE,
        IN_ERROR,
        CLOSED
    };
    void setState(State state) { state_ = state; }
    State getState() const { return state_; }
    
protected:
    State                   state_ = State::IDLE;
    
    std::string             method_;
    std::string             url_;
    std::string             version_;
    Uri                     uri_;
    
    DataCallback            data_cb_;
    EventCallback           write_cb_;
    EventCallback           error_cb_;
    HttpEventCallback       header_cb_;
    HttpEventCallback       response_cb_;
};

KUMA_NS_END

#endif
