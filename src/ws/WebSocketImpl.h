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

#ifndef __WebSocketImpl_H__
#define __WebSocketImpl_H__

#include "kmdefs.h"
#include "WSHandler.h"
#include "TcpSocketImpl.h"
#include "TcpConnection.h"
#include "http/Uri.h"
#include "util/DestroyDetector.h"

KUMA_NS_BEGIN

class WebSocketImpl : public KMObject, public DestroyDetector, public TcpConnection
{
public:
    typedef std::function<void(uint8_t*, size_t)> DataCallback;
    typedef std::function<void(int)> EventCallback;
    
    WebSocketImpl(EventLoopImpl* loop);
    ~WebSocketImpl();
    
    void setProtocol(const std::string& proto);
    const std::string& getProtocol() const { return proto_; }
    void setOrigin(const std::string& origin);
    const std::string& getOrigin() const { return origin_; }
    int connect(const std::string& ws_url, EventCallback cb);
    int attachFd(SOCKET_FD fd, const uint8_t* init_data = nullptr, size_t init_len = 0);
    int attachSocket(TcpSocketImpl&& tcp, HttpParserImpl&& parser);
    int send(const uint8_t* data, size_t len);
    int close();
    
    void setDataCallback(DataCallback cb) { data_cb_ = std::move(cb); }
    void setWriteCallback(EventCallback cb) { write_cb_ = std::move(cb); }
    void setErrorCallback(EventCallback cb) { error_cb_ = std::move(cb); }
    
private:
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
    int connect_i(const std::string& ws_url);
    void cleanup();
    
    void sendUpgradeRequest();
    void sendUpgradeResponse();
    void onWsData(uint8_t* data, size_t len);
    void onWsHandshake(int err);
    void onStateOpen();
    
    void onConnect(int err) override;
    KMError handleInputData(uint8_t *src, size_t len) override;
    void onWrite() override;
    void onError(int err) override;
    
private:
    State                   state_ = State::IDLE;
    WSHandler               ws_handler_;
    Uri                     uri_;
    
    size_t                  body_bytes_sent_ = 0;
    
    std::string             proto_;
    std::string             origin_;
    DataCallback            data_cb_;
    EventCallback           connect_cb_;
    EventCallback           write_cb_;
    EventCallback           error_cb_;
};

KUMA_NS_END

#endif
