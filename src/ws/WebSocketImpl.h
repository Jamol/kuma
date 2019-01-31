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
#include "TcpConnection.h"
#include "http/Uri.h"
#include "util/DestroyDetector.h"

#include <random>

KUMA_NS_BEGIN

class WebSocket::Impl : public KMObject, public DestroyDetector, public TcpConnection
{
public:
    using DataCallback = WebSocket::DataCallback;
    using EventCallback = WebSocket::EventCallback;
    using HandshakeCallback = WebSocket::HandshakeCallback;
    using EnumerateCallback = HttpParser::Impl::EnumerateCallback;
    
    Impl(const EventLoopPtr &loop);
    ~Impl();
    
    void setOrigin(const std::string& origin);
    const std::string& getOrigin() const { return origin_; }
    KMError setSubprotocol(const std::string& subprotocol);
    const std::string& getSubprotocol() const { return subprotocol_; }
    KMError setExtensions(const std::string& extensions);
    const std::string& getExtensions() const { return extensions_; }
    KMError addHeader(std::string name, std::string value);
    KMError addHeader(std::string name, uint32_t value);
    
    KMError connect(const std::string& ws_url, HandshakeCallback cb);
    KMError attachFd(SOCKET_FD fd, const KMBuffer *init_buf, HandshakeCallback cb);
    KMError attachSocket(TcpSocket::Impl&& tcp, HttpParser::Impl&& parser, const KMBuffer *init_buf, HandshakeCallback cb);
    int send(const void* data, size_t len, bool is_text, bool is_fin);
    int send(const KMBuffer &buf, bool is_text, bool is_fin);
    KMError close();
    
    const std::string& getPath() const
    {
        return ws_handler_.getPath();
    }
    const std::string& getQuery() const
    {
        return ws_handler_.getQuery();
    }
    const std::string& getParamValue(const std::string &name) const
    {
        return ws_handler_.getParamValue(name);
    }
    const std::string& getHeaderValue(const std::string &name) const
    {
        return ws_handler_.getHeaderValue(name);
    }
    void forEachHeader(const EnumerateCallback &cb) const
    {
        ws_handler_.forEachHeader(cb);
    }
    
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
    KMError connect_i(const std::string& ws_url);
    void cleanup();
    void setupWsHandler();
    
    std::string buildUpgradeRequest();
    std::string buildUpgradeResponse();
    void sendUpgradeRequest();
    void sendUpgradeResponse();
    void onWsFrame(uint8_t opcode, bool is_fin, KMBuffer &buf);
    void onWsHandshake(KMError err);
    void onStateOpen();
    KMError sendWsFrame(WSHandler::WSOpcode opcode, bool is_fin, uint8_t *payload, size_t plen);
    KMError sendWsFrame(WSHandler::WSOpcode opcode, bool is_fin, const KMBuffer &buf);
    KMError sendCloseFrame(uint16_t statusCode);
    KMError sendPingFrame(const KMBuffer &buf);
    KMError sendPongFrame(const KMBuffer &buf);
    
    void onConnect(KMError err) override;
    KMError handleInputData(uint8_t *src, size_t len) override;
    void onWrite() override;
    void onError(KMError err) override;
    
    uint32_t generateMaskKey();
    
private:
    State                   state_ = State::IDLE;
    WSHandler               ws_handler_;
    Uri                     uri_;
    bool                    fragmented_ = false;
    
    HeaderVector            header_vec_;
    
    KMError                 handshake_result_ = KMError::NOERR;
    size_t                  body_bytes_sent_ = 0;
    
    std::string             origin_;
    std::string             subprotocol_;
    std::string             extensions_;
    
    HandshakeCallback       handshake_cb_;
    DataCallback            data_cb_;
    EventCallback           write_cb_;
    EventCallback           error_cb_;
    
    std::mt19937 rand_engine_{std::random_device{}()};
};

KUMA_NS_END

#endif
