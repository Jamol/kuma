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

#include "WebSocketImpl.h"
#include "util/kmtrace.h"
#include "util/util.h"

#include <sstream>

using namespace kuma;

//////////////////////////////////////////////////////////////////////////
WebSocketImpl::WebSocketImpl(EventLoopImpl* loop)
: TcpConnection(loop)
{
    KM_SetObjKey("WebSocket");
}

WebSocketImpl::~WebSocketImpl()
{
    if(destroy_flag_ptr_) {
        *destroy_flag_ptr_ = true;
    }
}

void WebSocketImpl::cleanup()
{
    TcpConnection::close();
}

void WebSocketImpl::setProtocol(const std::string& proto)
{
    proto_ = proto;
}

void WebSocketImpl::setOrigin(const std::string& origin)
{
    origin_ = origin;
}

int WebSocketImpl::connect(const std::string& ws_url, EventCallback cb)
{
    if(getState() != State::IDLE) {
        KUMA_ERRXTRACE("connect, invalid state, state="<<getState());
        return KUMA_ERROR_INVALID_STATE;
    }
    connect_cb_ = std::move(cb);
    return connect_i(ws_url);
}

int WebSocketImpl::connect_i(const std::string& ws_url)
{
    if(!uri_.parse(ws_url)) {
        return false;
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

int WebSocketImpl::attachFd(SOCKET_FD fd, const uint8_t* init_data, size_t init_len)
{
    ws_handler_.setDataCallback([this] (uint8_t* data, size_t len) { onWsData(data, len); });
    ws_handler_.setHandshakeCallback([this] (int err) { onWsHandshake(err); });
    setState(State::UPGRADING);
    return TcpConnection::attachFd(fd, init_data, init_len);
}

int WebSocketImpl::attachSocket(TcpSocketImpl&& tcp, HttpParserImpl&& parser)
{
    ws_handler_.setDataCallback([this] (uint8_t* data, size_t len) { onWsData(data, len); });
    ws_handler_.setHandshakeCallback([this] (int err) { onWsHandshake(err); });
    setState(State::UPGRADING);

    int ret = TcpConnection::attachSocket(std::move(tcp));
    
    ws_handler_.setHttpParser(std::move(parser));
    return ret;
}

int WebSocketImpl::send(const uint8_t* data, size_t len)
{
    if(getState() != State::OPEN) {
        return -1;
    }
    if(!sendBufferEmpty()) {
        return 0;
    }
    uint8_t hdr[10];
    WSHandler::WSOpcode opcode = WSHandler::WSOpcode::WS_OPCODE_BINARY;
    if(ws_handler_.getOpcode() == WSHandler::WSOpcode::WS_OPCODE_TEXT) {
        opcode = WSHandler::WSOpcode::WS_OPCODE_TEXT;
    }
    int hdr_len = ws_handler_.encodeFrameHeader(opcode, len, hdr);
    iovec iovs[2];
    iovs[0].iov_base = (char*)hdr;
    iovs[0].iov_len = hdr_len;
    iovs[1].iov_base = (char*)data;
    iovs[1].iov_len = len;
    int ret = TcpConnection::send(iovs, 2);
    return ret < 0 ? ret : (int)len;
}

int WebSocketImpl::close()
{
    KUMA_INFOXTRACE("close");
    cleanup();
    setState(State::CLOSED);
    return KUMA_ERROR_NOERR;
}

KMError WebSocketImpl::handleInputData(uint8_t *src, size_t len)
{
    if (getState() == State::OPEN || getState() == State::UPGRADING) {
        bool destroyed = false;
        KUMA_ASSERT(nullptr == destroy_flag_ptr_);
        destroy_flag_ptr_ = &destroyed;
        WSHandler::WSError err = ws_handler_.handleData(src, len);
        if(destroyed) {
            return KUMA_ERROR_DESTROYED;
        }
        destroy_flag_ptr_ = nullptr;
        if(getState() == State::IN_ERROR || getState() == State::CLOSED) {
            return KUMA_ERROR_INVALID_STATE;
        }
        if(err != WSHandler::WSError::WS_ERROR_NOERR &&
           err != WSHandler::WSError::WS_ERROR_NEED_MORE_DATA) {
            cleanup();
            setState(State::CLOSED);
            if(error_cb_) error_cb_(KUMA_ERROR_FAILED);
            return KUMA_ERROR_FAILED;
        }
    } else {
        KUMA_WARNXTRACE("handleInputData, invalid state: "<<getState());
    }
    return KUMA_ERROR_NOERR;
}

void WebSocketImpl::onConnect(int err)
{
    if(err != KUMA_ERROR_NOERR) {
        if(connect_cb_) connect_cb_(err);
        return ;
    }
    ws_handler_.setDataCallback([this] (uint8_t* data, size_t len) { onWsData(data, len); });
    ws_handler_.setHandshakeCallback([this] (int err) { onWsHandshake(err); });
    body_bytes_sent_ = 0;
    sendUpgradeRequest();
}

void WebSocketImpl::onWrite()
{
    if(getState() == State::UPGRADING) {
        if (isServer()) {
            onStateOpen(); // response is sent out
        } else {
            return; // wait upgrade response
        }
    }
    if (write_cb_) write_cb_(0);
}

void WebSocketImpl::onError(int err)
{
    KUMA_INFOXTRACE("onError, err="<<err);
    cleanup();
    setState(State::CLOSED);
    if(error_cb_) error_cb_(err);
}

void WebSocketImpl::sendUpgradeRequest()
{
    std::string str(ws_handler_.buildUpgradeRequest(uri_.getPath(), uri_.getHost(), proto_, origin_));
    setState(State::UPGRADING);
    TcpConnection::send((const uint8_t*)str.c_str(), str.size());
}

void WebSocketImpl::sendUpgradeResponse()
{
    std::string str(ws_handler_.buildUpgradeResponse());
    setState(State::UPGRADING);
    int ret = TcpConnection::send((const uint8_t*)str.c_str(), str.size());
    if (ret == str.size()) {
        onStateOpen();
    }
}

void WebSocketImpl::onStateOpen()
{
    KUMA_INFOXTRACE("onStateOpen");
    setState(State::OPEN);
    if(isServer()) {
        if(write_cb_) write_cb_(0);
    } else {
        if(connect_cb_) connect_cb_(0);
    }
}

void WebSocketImpl::onWsData(uint8_t* data, size_t len)
{
    if(data_cb_) data_cb_(data, len);
}

void WebSocketImpl::onWsHandshake(int err)
{
    if(0 == err) {
        if(isServer()) {
            sendUpgradeResponse();
        } else {
            onStateOpen();
        }
    } else {
        setState(State::IN_ERROR);
        if(error_cb_) error_cb_(KUMA_ERROR_FAILED);
    }
}
