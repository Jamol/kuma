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
#include "http/Uri.h"

KUMA_NS_BEGIN

class WebSocketImpl
{
public:
    typedef std::function<void(uint8_t*, uint32_t)> DataCallback;
    typedef std::function<void(int)> EventCallback;
    
    WebSocketImpl(EventLoopImpl* loop);
    ~WebSocketImpl();
    
    void setProtocol(const std::string& proto);
    const std::string& getProtocol() const { return proto_; }
    void setOrigin(const std::string& origin);
    const std::string& getOrigin() const { return origin_; }
    int connect(const std::string& ws_url, const EventCallback& cb);
    int connect(const std::string& ws_url, EventCallback&& cb);
    int attachFd(SOCKET_FD fd, uint32_t flags, const uint8_t* init_data = nullptr, uint32_t init_len = 0);
    int attachSocket(TcpSocketImpl&& tcp, HttpParserImpl&& parser);
    int send(const uint8_t* data, uint32_t len);
    int close();
    
    void setDataCallback(const DataCallback& cb) { cb_data_ = cb; }
    void setWriteCallback(const EventCallback& cb) { cb_write_ = cb; }
    void setErrorCallback(const EventCallback& cb) { cb_error_ = cb; }
    void setDataCallback(DataCallback&& cb) { cb_data_ = std::move(cb); }
    void setWriteCallback(EventCallback&& cb) { cb_write_ = std::move(cb); }
    void setErrorCallback(EventCallback&& cb) { cb_error_ = std::move(cb); }
    
protected: // callbacks of tcp_socket
    void onConnect(int err);
    void onSend(int err);
    void onReceive(int err);
    void onClose(int err);
    
protected:
    const char* getObjKey() const;

private:
    enum State {
        STATE_IDLE,
        STATE_CONNECTING,
        STATE_HANDSHAKE,
        STATE_OPEN,
        STATE_ERROR,
        STATE_CLOSED
    };
    void setState(State state) { state_ = state; }
    State getState() { return state_; }
    int connect_i(const std::string& ws_url);
    void cleanup();
    
    void sendWsResponse();
    void onWsData(uint8_t* data, uint32_t len);
    void onWsHandshake(int err);
    void onStateOpen();
    
private:
    State                   state_;
    WSHandler               ws_handler_;
    Uri                     uri_;
    
    uint8_t*                init_data_;
    uint32_t                init_len_;
    
    std::vector<uint8_t>    send_buffer_;
    uint32_t                send_offset_;
    TcpSocketImpl           tcp_socket_;
    bool                    is_server_;
    
    uint32_t                body_bytes_sent_;
    
    std::string             proto_;
    std::string             origin_;
    DataCallback            cb_data_;
    EventCallback           cb_connect_;
    EventCallback           cb_write_;
    EventCallback           cb_error_;
    
    bool*                   destroy_flag_ptr_;
};

KUMA_NS_END

#endif
