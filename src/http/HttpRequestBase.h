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

#ifndef __HttpRequestBase_H__
#define __HttpRequestBase_H__

#include "kmdefs.h"
#include "httpdefs.h"
#include "Uri.h"
#include <map>

KUMA_NS_BEGIN

class HttpRequestBase
{
public:
    using DataCallback = std::function<void(uint8_t*, size_t)>;
    using EventCallback = std::function<void(KMError)>;
    using HttpEventCallback = std::function<void(void)>;
    using EnumrateCallback = std::function<void(const std::string&, const std::string&)>;
    
    virtual ~HttpRequestBase() = default;
    
    virtual KMError setSslFlags(uint32_t ssl_flags) = 0;
    void addHeader(std::string name, std::string value);
    void addHeader(std::string name, uint32_t value);
    KMError sendRequest(std::string method, std::string url, std::string ver);
    virtual int sendData(const uint8_t* data, size_t len) = 0;
    virtual void reset();
    virtual KMError close() = 0;
    
    virtual int getStatusCode() const = 0;
    virtual const std::string& getVersion() const = 0;
    virtual const std::string& getHeaderValue(std::string name) const = 0;
    virtual void forEachHeader(EnumrateCallback cb) = 0;
    
    void setDataCallback(DataCallback cb) { data_cb_ = std::move(cb); }
    void setWriteCallback(EventCallback cb) { write_cb_ = std::move(cb); }
    void setErrorCallback(EventCallback cb) { error_cb_ = std::move(cb); }
    void setHeaderCompleteCallback(HttpEventCallback cb) { header_cb_ = std::move(cb); }
    void setResponseCompleteCallback(HttpEventCallback cb) { response_cb_ = std::move(cb); }
    
protected:
    virtual KMError sendRequest() = 0;
    virtual void checkHeaders() = 0;
    virtual bool isVersion1_1() { return false; }
    
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
    
    HeaderMap               header_map_;
    std::string             method_;
    std::string             url_;
    std::string             version_;
    Uri                     uri_;
    
    bool                    has_content_length_ = false;
    size_t                  content_length_ = 0;
    bool                    is_chunked_ = false;
    size_t                  body_bytes_sent_ = 0;
    
    DataCallback            data_cb_;
    EventCallback           write_cb_;
    EventCallback           error_cb_;
    HttpEventCallback       header_cb_;
    HttpEventCallback       response_cb_;
};

KUMA_NS_END

#endif
