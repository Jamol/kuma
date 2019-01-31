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
#include "util/base64.h"

#include <sstream>

using namespace kuma;

static std::string generate_sec_accept_value(const std::string& sec_ws_key);

//////////////////////////////////////////////////////////////////////////
WebSocket::Impl::Impl(const EventLoopPtr &loop)
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
    ws_handler_.reset();
    header_vec_.clear();
    handshake_result_ = KMError::NOERR;
    body_bytes_sent_ = 0;
    fragmented_ = false;
    origin_.clear();
    subprotocol_.clear();
    extensions_.clear();
}

void WebSocket::Impl::setupWsHandler()
{
    ws_handler_.setFrameCallback([this] (uint8_t opcode, bool is_fin, KMBuffer &buf) {
        onWsFrame(opcode, is_fin, buf);
    });
    ws_handler_.setHandshakeCallback([this] (KMError err) {
        onWsHandshake(err);
    });
}

void WebSocket::Impl::setOrigin(const std::string& origin)
{
    origin_ = origin;
}

KMError WebSocket::Impl::setSubprotocol(const std::string& subprotocol)
{
    subprotocol_ = subprotocol;
    return KMError::NOERR;
}

KMError WebSocket::Impl::setExtensions(const std::string& extensions)
{
    extensions_ = extensions;
    return KMError::NOERR;
}

KMError WebSocket::Impl::addHeader(std::string name, std::string value)
{
    if (!name.empty() && !value.empty()) {
        header_vec_.emplace_back(std::move(name), std::move(value));
        return KMError::NOERR;
    }
    return KMError::INVALID_PARAM;
}

KMError WebSocket::Impl::addHeader(std::string name, uint32_t value)
{
    return addHeader(std::move(name), std::to_string(value));
}

KMError WebSocket::Impl::connect(const std::string& ws_url, HandshakeCallback cb)
{
    if(getState() != State::IDLE && getState() != State::CLOSED) {
        KUMA_ERRXTRACE("connect, invalid state, state="<<getState());
        return KMError::INVALID_STATE;
    }
    ws_handler_.setMode(WSHandler::WSMode::CLIENT);
    handshake_cb_ = std::move(cb);
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
    auto rv = setSslFlags(ssl_flags);
    if (rv != KMError::NOERR) {
        return rv;
    }
    return TcpConnection::connect(uri_.getHost().c_str(), port);
}

KMError WebSocket::Impl::attachFd(SOCKET_FD fd, const KMBuffer *init_buf, HandshakeCallback cb)
{
    handshake_cb_ = std::move(cb);
    ws_handler_.setMode(WSHandler::WSMode::SERVER);
    setState(State::UPGRADING);
    return TcpConnection::attachFd(fd, init_buf);
}

KMError WebSocket::Impl::attachSocket(TcpSocket::Impl&& tcp, HttpParser::Impl&& parser, const KMBuffer *init_buf, HandshakeCallback cb)
{
    handshake_cb_ = std::move(cb);
    ws_handler_.setMode(WSHandler::WSMode::SERVER);
    setState(State::UPGRADING);

    auto ret = TcpConnection::attachSocket(std::move(tcp), init_buf);
    
    ws_handler_.setHttpParser(std::move(parser));
    return ret;
}

int WebSocket::Impl::send(const void* data, size_t len, bool is_text, bool is_fin)
{
    if(getState() != State::OPEN) {
        return -1;
    }
    if(!sendBufferEmpty()) {
        return 0;
    }
    WSHandler::WSOpcode opcode = WSHandler::WSOpcode::WS_OPCODE_BINARY;
    if (fragmented_) {
        opcode = WSHandler::WSOpcode::WS_OPCODE_CONTINUE;
    }
    else if(is_text) {
        opcode = WSHandler::WSOpcode::WS_OPCODE_TEXT;
    }
    fragmented_ = !is_fin;
    auto ret = sendWsFrame(opcode, is_fin, (uint8_t*)data, len);
    return ret == KMError::NOERR ? (int)len : -1;
}

int WebSocket::Impl::send(const KMBuffer &buf, bool is_text, bool is_fin)
{
    if(getState() != State::OPEN) {
        return -1;
    }
    if(!sendBufferEmpty()) {
        return 0;
    }
    WSHandler::WSOpcode opcode = WSHandler::WSOpcode::WS_OPCODE_BINARY;
    if (fragmented_) {
        opcode = WSHandler::WSOpcode::WS_OPCODE_CONTINUE;
    }
    else if(is_text) {
        opcode = WSHandler::WSOpcode::WS_OPCODE_TEXT;
    }
    fragmented_ = !is_fin;
    auto chainSize = buf.chainLength();
    auto ret = sendWsFrame(opcode, is_fin, buf);
    return ret == KMError::NOERR ? static_cast<int>(chainSize) : -1;
}

KMError WebSocket::Impl::close()
{
    KUMA_INFOXTRACE("close");
    if (getState() == State::OPEN) {
        //sendCloseFrame(1000);
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
        if(handshake_cb_) handshake_cb_(err);
        return ;
    }
    body_bytes_sent_ = 0;
    sendUpgradeRequest();
}

void WebSocket::Impl::onWrite()
{
    if(getState() == State::UPGRADING) {
        if (isServer()) {
            // response is sent out
            if (handshake_result_ == KMError::NOERR) {
                onStateOpen();
            } else {
                setState(State::IN_ERROR);
                if(error_cb_) error_cb_(handshake_result_);
            }
            return;
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

std::string WebSocket::Impl::buildUpgradeRequest()
{
    std::stringstream ss;
    ss << "GET ";
    ss << uri_.getPath();
    auto str_query = uri_.getQuery();
    if(!str_query.empty()){
        ss << "?";
        ss << str_query;
    }
    ss << " HTTP/1.1\r\n";
    ss << "Host: " << uri_.getHost() << "\r\n";
    ss << "Upgrade: websocket\r\n";
    ss << "Connection: Upgrade\r\n";
    if (!origin_.empty()) {
        ss << "Origin: " << origin_ << "\r\n";
    }
    ss << kSecWebSocketKey << ": " << "dGhlIHNhbXBsZSBub25jZQ==" << "\r\n";
    if (!subprotocol_.empty()) {
        ss << kSecWebSocketProtocol << ": " << subprotocol_ << "\r\n";
    }
    if (!extensions_.empty()) {
        ss << kSecWebSocketExtensions << ": " << extensions_ << "\r\n";
    }
    ss << kSecWebSocketVersion << ": " << kWebSocketVersion << "\r\n";
    
    for (auto &kv : header_vec_) {
        ss << kv.first << ": " << kv.second << "\r\n";
    }
    
    ss << "\r\n";
    return ss.str();
}

std::string WebSocket::Impl::buildUpgradeResponse()
{
    if (handshake_result_ == KMError::NOERR) {
        std::string sec_ws_key = ws_handler_.getHeaderValue(kSecWebSocketKey);
        std::string client_protocols = ws_handler_.getHeaderValue(kSecWebSocketProtocol);
        std::string client_extensions = ws_handler_.getHeaderValue(kSecWebSocketExtensions);
        
        std::stringstream ss;
        ss << "HTTP/1.1 101 Switching Protocols\r\n";
        ss << "Upgrade: websocket\r\n";
        ss << "Connection: Upgrade\r\n";
        ss << kSecWebSocketAccept << ": " << generate_sec_accept_value(sec_ws_key) << "\r\n";
        if(!subprotocol_.empty()) {
            ss << kSecWebSocketProtocol << ": " << subprotocol_ << "\r\n";
        }
        if (!extensions_.empty()) {
            ss << kSecWebSocketExtensions << ": " << extensions_ << "\r\n";
        }
        
        for (auto &kv : header_vec_) {
            ss << kv.first << ": " << kv.second << "\r\n";
        }
        
        ss << "\r\n";
        return ss.str();
    } else {
        std::stringstream ss;
        if (handshake_result_ == KMError::REJECTED) {
            ss << "HTTP/1.1 403 Forbidden\r\n";
        } else {
            ss << "HTTP/1.1 400 Bad Request\r\n";
        }
        ss << kSecWebSocketVersion << ": " << kWebSocketVersion << "\r\n";
        
        for (auto &kv : header_vec_) {
            ss << kv.first << ": " << kv.second << "\r\n";
        }
        
        ss << "\r\n";
        return ss.str();
    }
}

void WebSocket::Impl::sendUpgradeRequest()
{
    std::string str(buildUpgradeRequest());
    setState(State::UPGRADING);
    TcpConnection::send((const uint8_t*)str.c_str(), str.size());
}

void WebSocket::Impl::sendUpgradeResponse()
{
    std::string str(buildUpgradeResponse());
    setState(State::UPGRADING);
    int ret = TcpConnection::send((const uint8_t*)str.c_str(), str.size());
    if (ret == (int)str.size()) {
        if (handshake_result_ == KMError::NOERR) {
            onStateOpen();
        } else {
            setState(State::IN_ERROR);
            if(error_cb_) error_cb_(handshake_result_);
        }
    }
}

void WebSocket::Impl::onStateOpen()
{
    KUMA_INFOXTRACE("onStateOpen");
    setState(State::OPEN);
    if(isServer()) {
        if(write_cb_) write_cb_(KMError::NOERR);
    } else {
        if(handshake_cb_) handshake_cb_(KMError::NOERR);
    }
}

void WebSocket::Impl::onWsFrame(uint8_t opcode, bool is_fin, KMBuffer &buf)
{
    if (WSHandler::isControlFrame(opcode)) {
        auto buf_len = buf.chainLength();
        if (WSHandler::WSOpcode::WS_OPCODE_CLOSE == opcode) {
            uint16_t statusCode = 0;
            if (buf_len >= 2) {
                uint8_t hdr[2];
                const KMBuffer &const_buf = buf;
                const_buf.readChained(hdr, 2);
                statusCode = (hdr[0] << 8) | hdr[1];
                KUMA_INFOXTRACE("onWsFrame, close-frame, statusCode="<<statusCode<<", plen="<<(buf_len-2));
            } else {
                KUMA_INFOXTRACE("onWsFrame, close-frame received");
            }
            sendCloseFrame(statusCode);
            cleanup();
            setState(State::CLOSED);
            if(error_cb_) error_cb_(KMError::CLOSED);
        } else if (WSHandler::WSOpcode::WS_OPCODE_PING == opcode) {
            sendPongFrame(buf);
        }
    } else {
        bool is_text = WSHandler::WSOpcode::WS_OPCODE_TEXT == opcode;
        if(data_cb_) data_cb_(buf, is_text, is_fin);
    }
}

void WebSocket::Impl::onWsHandshake(KMError err)
{
    handshake_result_ = err;
    if (isServer()) {
        origin_ = ws_handler_.getOrigin();
        if(handshake_cb_ && KMError::NOERR == err) {
            DESTROY_DETECTOR_SETUP();
            auto rv = handshake_cb_(err);
            DESTROY_DETECTOR_CHECK_VOID();
            if (!rv) {
                handshake_result_ = KMError::REJECTED;
            }
        }
        sendUpgradeResponse();
    } else {
        if(KMError::NOERR == err) {
            onStateOpen();
        } else {
            setState(State::IN_ERROR);
            if(error_cb_) error_cb_(err);
        }
    }
}

KMError WebSocket::Impl::sendWsFrame(WSHandler::WSOpcode opcode, bool is_fin, uint8_t *payload, size_t plen)
{
    uint8_t hdr_buf[WS_MAX_HEADER_SIZE];
    int hdr_len = 0;
    if (ws_handler_.getMode() == WSHandler::WSMode::CLIENT && plen > 0) {
        uint8_t mask_key[WS_MASK_KEY_SIZE];
        *(uint32_t*)mask_key = generateMaskKey();
        WSHandler::handleDataMask(mask_key, payload, plen);
        hdr_len = ws_handler_.encodeFrameHeader(opcode, is_fin, &mask_key, plen, hdr_buf);
    } else {
        hdr_len = ws_handler_.encodeFrameHeader(opcode, is_fin, nullptr, plen, hdr_buf);
    }
    iovec iovs[2];
    int cnt = 0;
    iovs[0].iov_base = (char*)hdr_buf;
    iovs[0].iov_len = hdr_len;
    ++cnt;
    if (plen > 0) {
        iovs[1].iov_base = (char*)payload;
        iovs[1].iov_len = static_cast<decltype(iovs[1].iov_len)>(plen);
        ++cnt;
    }
    auto ret = TcpConnection::send(iovs, cnt);
    return ret < 0 ? KMError::SOCK_ERROR : KMError::NOERR;
}

KMError WebSocket::Impl::sendWsFrame(WSHandler::WSOpcode opcode, bool is_fin, const KMBuffer &buf)
{
    size_t plen = buf.chainLength();
    uint8_t hdr_buf[WS_MAX_HEADER_SIZE];
    int hdr_len = 0;
    if (ws_handler_.getMode() == WSHandler::WSMode::CLIENT && plen > 0) {
        uint8_t mask_key[WS_MASK_KEY_SIZE];
        *(uint32_t*)mask_key = generateMaskKey();
        WSHandler::handleDataMask(mask_key, const_cast<KMBuffer&>(buf));
        hdr_len = ws_handler_.encodeFrameHeader(opcode, is_fin, &mask_key, plen, hdr_buf);
    } else {
        hdr_len = ws_handler_.encodeFrameHeader(opcode, is_fin, nullptr, plen, hdr_buf);
    }
    IOVEC iovs;
    iovec iov;
    iov.iov_base = (char*)hdr_buf;
    iov.iov_len = hdr_len;
    iovs.emplace_back(iov);
    buf.fillIov(iovs);
    auto ret = TcpConnection::send(&iovs[0], static_cast<int>(iovs.size()));
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

KMError WebSocket::Impl::sendPingFrame(const KMBuffer &buf)
{
    return sendWsFrame(WSHandler::WSOpcode::WS_OPCODE_PING, true, buf);
}

KMError WebSocket::Impl::sendPongFrame(const KMBuffer &buf)
{
    return sendWsFrame(WSHandler::WSOpcode::WS_OPCODE_PONG, true, buf);
}

uint32_t WebSocket::Impl::generateMaskKey()
{
    std::uniform_int_distribution<uint32_t> dist;
    return dist(rand_engine_);
}

#ifdef KUMA_HAS_OPENSSL
#include <openssl/sha.h>
#else
#include "sha1/sha1.h"
#include "sha1/sha1.cpp"
#define SHA1 sha1::calc
#endif

#define SHA1_DIGEST_SIZE    20
static std::string generate_sec_accept_value(const std::string& sec_ws_key)
{
    const std::string sec_accept_guid = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    if(sec_ws_key.empty()) {
        return "";
    }
    std::string accept = sec_ws_key;
    accept += sec_accept_guid;
    
    uint8_t uShaRst2[SHA1_DIGEST_SIZE] = {0};
    SHA1((const uint8_t *)accept.c_str(), accept.size(), uShaRst2);
    
    uint8_t x64_encode_buf[32] = {0};
    uint32_t x64_encode_len = x64_encode(uShaRst2, SHA1_DIGEST_SIZE, x64_encode_buf, sizeof(x64_encode_buf), false);
    
    return std::string((char*)x64_encode_buf, x64_encode_len);
}
