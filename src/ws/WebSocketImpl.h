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

#ifndef __WebSocketImpl_H__
#define __WebSocketImpl_H__

#include "kmdefs.h"
#include "kmapi.h"
#include "WSHandler.h"
#include "WSConnection.h"
#include "EventLoopImpl.h"
#include "http/Uri.h"
#include "util/DestroyDetector.h"

#include <random>

WS_NS_BEGIN

class WSConnection;
class ExtensionHandler;

WS_NS_END

KUMA_NS_BEGIN

class WebSocket::Impl : public KMObject, public DestroyDetector
{
public:
    using DataCallback = WebSocket::DataCallback;
    using EventCallback = WebSocket::EventCallback;
    using HandshakeCallback = WebSocket::HandshakeCallback;
    using EnumerateCallback = HttpParser::Impl::EnumerateCallback;
    
    Impl(const EventLoopPtr &loop, const std::string &http_ver);
    ~Impl();
    
    void setOrigin(const std::string& origin) { ws_conn_->setOrigin(origin); }
    const std::string& getOrigin() const { return ws_conn_->getOrigin(); }
    KMError setSubprotocol(const std::string& subprotocol) { return ws_conn_->setSubprotocol(subprotocol); }
    const std::string& getSubprotocol() const { return ws_conn_->getSubprotocol(); }
    const std::string& getExtensions() const { return ws_conn_->getExtensions(); }
    KMError addHeader(std::string name, std::string value)
    {
        return ws_conn_->addHeader(std::move(name), std::move(value));
    }
    KMError addHeader(std::string name, uint32_t value)
    {
        return ws_conn_->addHeader(std::move(name), value);
    }
    
    KMError setSslFlags(uint32_t ssl_flags);
    KMError connect(const std::string& ws_url);
    KMError attachFd(SOCKET_FD fd, const KMBuffer *init_buf, HandshakeCallback cb);
    KMError attachSocket(TcpSocket::Impl&& tcp, HttpParser::Impl&& parser, const KMBuffer *init_buf, HandshakeCallback cb);
    KMError attachStream(uint32_t stream_id, H2Connection::Impl* conn, HandshakeCallback cb);
    int send(const void* data, size_t len, bool is_text, bool is_fin, uint32_t flags);
    int send(const KMBuffer &buf, bool is_text, bool is_fin, uint32_t flags);
    KMError close();
    
    const std::string& getPath() const
    {
        return ws_conn_->getPath();
    }
    const std::string& getHeaderValue(const std::string &name) const
    {
        return ws_conn_->getHeaders().getHeader(name);
    }
    void forEachHeader(const EnumerateCallback &cb) const
    {
        auto const &http_header = ws_conn_->getHeaders();
        for (auto const &kv : http_header.getHeaders()) {
            if (!cb(kv.first, kv.second)) {
                break;
            }
        }
    }
    
    void setOpenCallback(EventCallback cb) { open_cb_ = std::move(cb); }
    void setDataCallback(DataCallback cb) { data_cb_ = std::move(cb); }
    void setWriteCallback(EventCallback cb) { write_cb_ = std::move(cb); }
    void setErrorCallback(EventCallback cb) { error_cb_ = std::move(cb); }
    
private:
    enum State {
        IDLE,
        HANDSHAKE,
        OPEN,
        IN_ERROR,
        CLOSED
    };
    void setState(State state) { state_ = state; }
    State getState() { return state_; }
    bool isServer() const;
    void cleanup();
    
    KMError negotiateExtensions();
    KMError onWsFrame(ws::FrameHeader hdr, KMBuffer &buf);
    bool onWsHandshake(KMError err);
    void onWsOpen(KMError err);
    void onWsData(KMBuffer &buf);
    void onWsWrite();
    void onWsError(KMError err);
    void onStateOpen();
    KMError sendWsFrame(ws::FrameHeader hdr, uint8_t *payload, size_t plen);
    KMError sendWsFrame(ws::FrameHeader hdr, const KMBuffer &buf);
    KMError sendCloseFrame(uint16_t statusCode);
    KMError sendPingFrame(const KMBuffer &buf);
    KMError sendPongFrame(const KMBuffer &buf);
    
    void onError(KMError err);
    
    KMError onExtensionIncomingFrame(ws::FrameHeader hdr, KMBuffer &buf);
    KMError onExtensionOutgoingFrame(ws::FrameHeader hdr, KMBuffer &buf);
    
    uint32_t generateMaskKey();
    
private:
    State                   state_ = State::IDLE;
    ws::WSHandler           ws_handler_;
    bool                    fragmented_ = false;
    
    size_t                  body_bytes_sent_ = 0;
    
    HandshakeCallback       handshake_cb_;
    EventCallback           open_cb_;
    DataCallback            data_cb_;
    EventCallback           write_cb_;
    EventCallback           error_cb_;
    
    std::mt19937            rand_engine_{std::random_device{}()};
    
    std::unique_ptr<ws::WSConnection>       ws_conn_;
    std::unique_ptr<ws::ExtensionHandler>   extension_handler_;
};

KUMA_NS_END

#endif
