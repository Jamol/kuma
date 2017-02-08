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

#include "WebSocketImpl.h"
#include "util/kmtrace.h"
#include "util/util.h"

#include <sstream>

using namespace kuma;

//////////////////////////////////////////////////////////////////////////
WebSocket::Impl::Impl(EventLoop::Impl* loop)
: TcpConnection(loop)
{
    setupWsHandler();
    KM_SetObjKey("WebSocket");
}

WebSocket::Impl::~Impl()
{
    
}

void WebSocket::Impl::cleanup()
{
    TcpConnection::close();
}

void WebSocket::Impl::setupWsHandler()
{
    ws_handler_.setFrameCallback([this] (uint8_t opcode, bool fin, void* data, size_t len) {
        onWsFrame(opcode, fin, data, len);
    });
    ws_handler_.setHandshakeCallback([this] (KMError err) {
        onWsHandshake(err);
    });
}

void WebSocket::Impl::setProtocol(const std::string& proto)
{
    proto_ = proto;
}

void WebSocket::Impl::setOrigin(const std::string& origin)
{
    origin_ = origin;
}

KMError WebSocket::Impl::connect(const std::string& ws_url, EventCallback cb)
{
    if(getState() != State::IDLE) {
        KUMA_ERRXTRACE("connect, invalid state, state="<<getState());
        return KMError::INVALID_STATE;
    }
    ws_handler_.setMode(WSHandler::WSMode::CLIENT);
    connect_cb_ = std::move(cb);
    return connect_i(ws_url);
}

KMError WebSocket::Impl::connect_i(const std::string& ws_url)
{
    if(!uri_.parse(ws_url)) {
        return KMError::INVALID_PARAM;
    }
    setState(State::CONNECTING);
    std::string str_port = uri_.getPort();
    uint16_t port = 80;
    uint32_t ssl_flags = SSL_NONE;
    if(is_equal("wss", uri_.getScheme())) {
        port = 443;
        ssl_flags = SSL_ENABLE | getSslFlags();
    }
    if(!str_port.empty()) {
        port = std::stoi(str_port);
    }
    setSslFlags(ssl_flags);
    return TcpConnection::connect(uri_.getHost().c_str(), port);
}

KMError WebSocket::Impl::attachFd(SOCKET_FD fd, const void* init_data, size_t init_len)
{
    ws_handler_.setMode(WSHandler::WSMode::SERVER);
    setState(State::UPGRADING);
    return TcpConnection::attachFd(fd, init_data, init_len);
}

KMError WebSocket::Impl::attachSocket(TcpSocket::Impl&& tcp, HttpParser::Impl&& parser, const void* init_data, size_t init_len)
{
    ws_handler_.setMode(WSHandler::WSMode::SERVER);
    setState(State::UPGRADING);

    auto ret = TcpConnection::attachSocket(std::move(tcp), init_data, init_len);
    
    ws_handler_.setHttpParser(std::move(parser));
    return ret;
}

int WebSocket::Impl::send(const void* data, size_t len, bool is_text, bool fin)
{
    if(getState() != State::OPEN) {
        return -1;
    }
    if(!sendBufferEmpty()) {
        return 0;
    }
    WSHandler::WSOpcode opcode = WSHandler::WSOpcode::WS_OPCODE_BINARY;
    if(is_text) {
        opcode = WSHandler::WSOpcode::WS_OPCODE_TEXT;
    }
    if (fin) {
        if (fragmented_) {
            fragmented_ = false;
            opcode = WSHandler::WSOpcode::WS_OPCODE_CONTINUE;
        }
    } else {
        if (fragmented_) {
            opcode = WSHandler::WSOpcode::WS_OPCODE_CONTINUE;
        }
        fragmented_ = true;
    }
    auto ret = sendWsFrame(opcode, fin, (uint8_t*)data, len);
    return ret == KMError::NOERR ? (int)len : -1;
}

KMError WebSocket::Impl::close()
{
    KUMA_INFOXTRACE("close");
    if (getState() == State::OPEN) {
        sendCloseFrame(1000);
    }
    cleanup();
    setState(State::CLOSED);
    return KMError::NOERR;
}

KMError WebSocket::Impl::handleInputData(uint8_t *src, size_t len)
{
    if (getState() == State::OPEN || getState() == State::UPGRADING) {
        DESTROY_DETECTOR_SETUP();
        WSHandler::WSError err = ws_handler_.handleData(src, len);
        DESTROY_DETECTOR_CHECK(KMError::DESTROYED);
        if(getState() == State::IN_ERROR || getState() == State::CLOSED) {
            return KMError::INVALID_STATE;
        }
        if(err != WSHandler::WSError::NOERR &&
           err != WSHandler::WSError::NEED_MORE_DATA) {
            onError(KMError::FAILED);
            return KMError::FAILED;
        }
    } else {
        KUMA_WARNXTRACE("handleInputData, invalid state: "<<getState());
    }
    return KMError::NOERR;
}

void WebSocket::Impl::onConnect(KMError err)
{
    if(err != KMError::NOERR) {
        if(connect_cb_) connect_cb_(err);
        return ;
    }
    body_bytes_sent_ = 0;
    sendUpgradeRequest();
}

void WebSocket::Impl::onWrite()
{
    if(getState() == State::UPGRADING) {
        if (isServer()) {
            onStateOpen(); // response is sent out
        } else {
            return; // wait upgrade response
        }
    }
    if (write_cb_) write_cb_(KMError::NOERR);
}

void WebSocket::Impl::onError(KMError err)
{
    KUMA_INFOXTRACE("onError, err="<<int(err));
    cleanup();
    setState(State::CLOSED);
    if(error_cb_) error_cb_(err);
}

void WebSocket::Impl::sendUpgradeRequest()
{
    std::string str(ws_handler_.buildUpgradeRequest(uri_.getPath(), uri_.getHost(), proto_, origin_));
    setState(State::UPGRADING);
    TcpConnection::send((const uint8_t*)str.c_str(), str.size());
}

void WebSocket::Impl::sendUpgradeResponse()
{
    std::string str(ws_handler_.buildUpgradeResponse());
    setState(State::UPGRADING);
    int ret = TcpConnection::send((const uint8_t*)str.c_str(), str.size());
    if (ret == (int)str.size()) {
        onStateOpen();
    }
}

void WebSocket::Impl::onStateOpen()
{
    KUMA_INFOXTRACE("onStateOpen");
    setState(State::OPEN);
    if(isServer()) {
        if(write_cb_) write_cb_(KMError::NOERR);
    } else {
        if(connect_cb_) connect_cb_(KMError::NOERR);
    }
}

void WebSocket::Impl::onWsFrame(uint8_t opcode, bool fin, void* payload, size_t plen)
{
    if (WSHandler::isControlFrame(opcode)) {
        if (WSHandler::WSOpcode::WS_OPCODE_CLOSE == opcode) {
            uint16_t statusCode = 0;
            if (payload && plen >= 2) {
                const uint8_t *ptr = (uint8_t*)payload;
                statusCode = (ptr[0] << 8) | ptr[1];
                KUMA_INFOXTRACE("onWsFrame, close-frame, statusCode="<<statusCode<<", plen="<<(plen-2));
            } else {
                KUMA_INFOXTRACE("onWsFrame, close-frame received");
            }
            sendCloseFrame(statusCode);
            cleanup();
            setState(State::CLOSED);
            if(error_cb_) error_cb_(KMError::FAILED);
        } else if (WSHandler::WSOpcode::WS_OPCODE_PING == opcode) {
            sendPongFrame((uint8_t*)payload, plen);
        }
    } else {
        bool is_text = WSHandler::WSOpcode::WS_OPCODE_TEXT == opcode;
        if(data_cb_) data_cb_(payload, plen, is_text, fin);
    }
}

void WebSocket::Impl::onWsHandshake(KMError err)
{
    if(KMError::NOERR == err) {
        if(isServer()) {
            sendUpgradeResponse();
        } else {
            onStateOpen();
        }
    } else {
        setState(State::IN_ERROR);
        if(error_cb_) error_cb_(KMError::FAILED);
    }
}

KMError WebSocket::Impl::sendWsFrame(WSHandler::WSOpcode opcode, bool fin, uint8_t *payload, size_t plen)
{
    uint8_t hdr_buf[WS_MAX_HEADER_SIZE];
    int hdr_len = 0;
    if (ws_handler_.getMode() == WSHandler::WSMode::CLIENT && plen > 0) {
        uint8_t mask_key[WS_MASK_KEY_SIZE];
        generateRandomBytes(mask_key, WS_MASK_KEY_SIZE);
        WSHandler::handleDataMask(mask_key, payload, plen);
        hdr_len = ws_handler_.encodeFrameHeader(opcode, fin, &mask_key, plen, hdr_buf);
    } else {
        hdr_len = ws_handler_.encodeFrameHeader(opcode, fin, nullptr, plen, hdr_buf);
    }
    iovec iovs[2];
    int cnt = 0;
    iovs[0].iov_base = (char*)hdr_buf;
    iovs[0].iov_len = hdr_len;
    ++cnt;
    if (plen > 0) {
        iovs[1].iov_base = (char*)payload;
        iovs[1].iov_len = plen;
        ++cnt;
    }
    auto ret = TcpConnection::send(iovs, cnt);
    return ret < 0 ? KMError::SOCK_ERROR : KMError::NOERR;
}

KMError WebSocket::Impl::sendCloseFrame(uint16_t statusCode)
{
    if (statusCode != 0) {
        uint8_t payload[2];
        payload[0] = statusCode >> 8;
        payload[1] = statusCode & 0xFF;
        return sendWsFrame(WSHandler::WSOpcode::WS_OPCODE_CLOSE, true, payload, 2);
    } else {
        return sendWsFrame(WSHandler::WSOpcode::WS_OPCODE_CLOSE, true, nullptr, 0);
    }
}

KMError WebSocket::Impl::sendPingFrame(uint8_t *payload, size_t plen)
{
    return sendWsFrame(WSHandler::WSOpcode::WS_OPCODE_PING, true, payload, plen);
}

KMError WebSocket::Impl::sendPongFrame(uint8_t *payload, size_t plen)
{
    return sendWsFrame(WSHandler::WSOpcode::WS_OPCODE_PONG, true, payload, plen);
}
