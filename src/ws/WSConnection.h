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

#pragma once

#include "kmdefs.h"
#include "wsdefs.h"
#include "kmbuffer.h"
#include "http/httpdefs.h"
#include "http/HttpHeader.h"

#include <functional>

WS_NS_BEGIN

class WSConnection
{
public:
    using HandshakeCallback = std::function<bool(KMError)>;
    using DataCallback = std::function<void(KMBuffer &buf)>;
    using EventCallback = std::function<void(KMError)>;
    
    WSConnection();
    virtual ~WSConnection() {}
    
    void setOrigin(const std::string& origin) { origin_ = origin; }
    const std::string& getOrigin() const { return origin_; }
    KMError setSubprotocol(const std::string& subprotocol);
    const std::string& getSubprotocol() const { return subprotocol_; }
    KMError setExtensions(const std::string& extensions);
    const std::string& getExtensions() const { return extensions_; }
    virtual KMError addHeader(std::string name, std::string value) = 0;
    virtual KMError addHeader(std::string name, uint32_t value) = 0;
    
    virtual KMError setSslFlags(uint32_t ssl_flags) = 0;
    virtual KMError connect(const std::string& ws_url) = 0;
    virtual int send(const iovec* iovs, int count) = 0;
    virtual KMError close() = 0;
    virtual bool canSendData() const = 0;
    virtual const std::string& getPath() const = 0;
    virtual const HttpHeader& getHeaders() const = 0;
    
    void setOpenCallback(EventCallback cb) { open_cb_ = std::move(cb); }
    void setDataCallback(DataCallback cb) { data_cb_ = std::move(cb); }
    void setWriteCallback(EventCallback cb) { write_cb_ = std::move(cb); }
    void setErrorCallback(EventCallback cb) { error_cb_ = std::move(cb); }
    
protected:
    enum State {
        IDLE,
        CONNECTING,
        UPGRADING,
        OPEN,
        IN_ERROR,
        CLOSED
    };
    void setState(State state) { state_ = state; }
    State getState() { return state_; }
    void onStateOpen();
    virtual void onStateError(KMError err);
    
protected:
    State                   state_ = State::IDLE;
    
    std::string             origin_;
    std::string             subprotocol_;
    std::string             extensions_;
    
    HandshakeCallback       handshake_cb_;
    EventCallback           open_cb_;
    DataCallback            data_cb_;
    EventCallback           write_cb_;
    EventCallback           error_cb_;
};

WS_NS_END
