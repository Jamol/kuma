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
#include "EventLoopImpl.h"
#include "TcpConnection.h"
#include "http/HttpParserImpl.h"
#include "ProxyAuthenticator.h"

KUMA_NS_BEGIN

class ProxyConnection::Impl : public TcpConnection
{
public:
    using EventCallback = ProxyConnection::EventCallback;
    using DataCallback = ProxyConnection::DataCallback;

    Impl(const EventLoopPtr &loop);
    virtual ~Impl();
    
    KMError setProxyInfo(const std::string &proxy_url, const std::string &user, const std::string &passwd);
    KMError connect(const std::string &host, uint16_t port, EventCallback cb) override;
    
    void setDataCallback(DataCallback cb) override { proxy_data_cb_ = std::move(cb); }
    void setWriteCallback(EventCallback cb) override { proxy_write_cb_ = std::move(cb); }
    void setErrorCallback(EventCallback cb) override { proxy_error_cb_ = std::move(cb); }
    
protected: // callbacks of TcpConnection
    void onTcpConnect(KMError err);
    KMError onTcpData(uint8_t *data, size_t size);
    void onTcpWrite();
    void onTcpError(KMError err);
    
protected:
    KMError sendProxyRequest();
    std::string buildProxyRequest();
    KMError handleProxyResponse();
    
    void onProxyConnect(KMError err);
    void onProxyData(uint8_t *data, size_t size);
    void onProxyWrite();
    void onProxyError(KMError err);
    
    void onHttpData(KMBuffer &buf);
    void onHttpEvent(HttpEvent ev);
    
    enum class State
    {
        IDLE,
        CONNECTING,
        AUTHENTICATING,
        OPEN,
        CLOSED
    };
    void setState(State state) { state_ = state; }
    State getState() const { return state_; }
    
private:
    State                   state_ = State::IDLE;
    std::string             connect_host_;
    uint16_t                connect_port_;
    std::string             proxy_addr_;
    uint16_t                proxy_port_;
    std::string             proxy_user_; // domain\user
    std::string             proxy_passwd_;
    HttpParser::Impl        http_parser_;
    std::unique_ptr<ProxyAuthenticator> proxy_auth_;
    
    bool                    need_reconnect_ = false;
    
    EventCallback           proxy_connect_cb_;
    DataCallback            proxy_data_cb_;
    EventCallback           proxy_write_cb_;
    EventCallback           proxy_error_cb_;
};

KUMA_NS_END
