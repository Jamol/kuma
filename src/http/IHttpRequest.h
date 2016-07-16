/* Copyright (c) 2014, Fengping Bao <jamol@live.com>
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

#ifndef __IHttpRequest_H__
#define __IHttpRequest_H__

#include "kmdefs.h"
#include "httpdefs.h"
#include "Uri.h"
#include <map>

KUMA_NS_BEGIN

struct CaseIgnoreLess : public std::binary_function<std::string, std::string, bool> {
    bool operator()(const std::string &lhs, const std::string &rhs) const {
        return strcasecmp(lhs.c_str(), rhs.c_str()) < 0;
    }
};
using KeyValueMap = std::map<std::string, std::string, CaseIgnoreLess>;

class IHttpRequest
{
public:
    using DataCallback = std::function<void(uint8_t*, size_t)>;
    using EventCallback = std::function<void(int)>;
    using HttpEventCallback = std::function<void(void)>;
    using EnumrateCallback = std::function<void(const std::string&, const std::string&)>;
    
    virtual ~IHttpRequest() {}
    
    virtual int setSslFlags(uint32_t ssl_flags) = 0;
    void addHeader(const std::string& name, const std::string& value);
    void addHeader(const std::string& name, uint32_t value);
    int sendRequest(const std::string& method, const std::string& url, const std::string& ver);
    virtual int sendData(const uint8_t* data, size_t len) = 0;
    virtual void reset();
    virtual int close() = 0;
    
    virtual int getStatusCode() const = 0;
    virtual const std::string& getVersion() const = 0;
    virtual const std::string& getHeaderValue(const char* name) const = 0;
    virtual void forEachHeader(EnumrateCallback cb) = 0;
    
    void setDataCallback(DataCallback cb) { cb_data_ = std::move(cb); }
    void setWriteCallback(EventCallback cb) { cb_write_ = std::move(cb); }
    void setErrorCallback(EventCallback cb) { cb_error_ = std::move(cb); }
    void setHeaderCompleteCallback(HttpEventCallback cb) { cb_header_ = std::move(cb); }
    void setResponseCompleteCallback(HttpEventCallback cb) { cb_response_ = std::move(cb); }
    
protected:
    virtual int sendRequest() = 0;
    virtual void checkHeaders() = 0;
    
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
    
    virtual const char* getObjKey() const = 0;
    
protected:
    State                   state_ = State::IDLE;
    
    KeyValueMap             header_map_;
    std::string             method_;
    std::string             url_;
    std::string             version_;
    Uri                     uri_;
    
    uint32_t                body_bytes_sent_ = 0;
    
    DataCallback            cb_data_;
    EventCallback           cb_write_;
    EventCallback           cb_error_;
    HttpEventCallback       cb_header_;
    HttpEventCallback       cb_response_;
};

KUMA_NS_END

#endif
