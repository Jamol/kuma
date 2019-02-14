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
#include "exts/ExtensionHandler.h"

#include <sstream>

using namespace kuma;
using namespace kuma::ws;

#define WS_FLAG_NO_COMPRESS(flags) (flags & 0x01)

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
    setupWsHandler();
    header_vec_.clear();
    handshake_result_ = KMError::NOERR;
    body_bytes_sent_ = 0;
    fragmented_ = false;
    origin_.clear();
    subprotocol_.clear();
    extensions_.clear();
    extension_handler_.reset();
}

void WebSocket::Impl::setupWsHandler()
{
    ws_handler_.setFrameCallback([this] (FrameHeader hdr, KMBuffer &buf) {
        return onWsFrame(hdr, buf);
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
    ws_handler_.setMode(WSMode::CLIENT);
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
    ws_handler_.setMode(WSMode::SERVER);
    setState(State::UPGRADING);
    return TcpConnection::attachFd(fd, init_buf);
}

KMError WebSocket::Impl::attachSocket(TcpSocket::Impl&& tcp, HttpParser::Impl&& parser, const KMBuffer *init_buf, HandshakeCallback cb)
{
    handshake_cb_ = std::move(cb);
    ws_handler_.setMode(WSMode::SERVER);
    setState(State::UPGRADING);

    auto ret = TcpConnection::attachSocket(std::move(tcp), init_buf);
    
    ws_handler_.setHttpParser(std::move(parser));
    return ret;
}

int WebSocket::Impl::send(const void* data, size_t len, bool is_text, bool is_fin, uint32_t flags)
{
    if(getState() != State::OPEN) {
        return -1;
    }
    if(!sendBufferEmpty()) {
        return 0;
    }
    auto opcode = WSOpcode::BINARY;
    if (fragmented_) {
        opcode = WSOpcode::CONTINUE;
    }
    else if(is_text) {
        opcode = WSOpcode::TEXT;
    }
    fragmented_ = !is_fin;
    KMError ret = KMError::FAILED;
    
    FrameHeader hdr;
    memset(&hdr, 0, sizeof(hdr));
    hdr.fin = is_fin ? 1 : 0;
    hdr.opcode = uint8_t(opcode);
    if (extension_handler_ && !WS_FLAG_NO_COMPRESS(flags)) {
        KMBuffer buf(data, len, len);
        ret = extension_handler_->handleOutcomingFrame(hdr, buf);
    } else {
        ret = sendWsFrame(hdr, (uint8_t*)data, len);
    }
    return ret == KMError::NOERR ? (int)len : -1;
}

int WebSocket::Impl::send(const KMBuffer &buf, bool is_text, bool is_fin, uint32_t flags)
{
    if(getState() != State::OPEN) {
        return -1;
    }
    if(!sendBufferEmpty()) {
        return 0;
    }
    auto opcode = WSOpcode::BINARY;
    if (fragmented_) {
        opcode = WSOpcode::CONTINUE;
    }
    else if(is_text) {
        opcode = WSOpcode::TEXT;
    }
    fragmented_ = !is_fin;
    auto chainSize = buf.chainLength();
    
    FrameHeader hdr;
    memset(&hdr, 0, sizeof(hdr));
    hdr.fin = is_fin ? 1 : 0;
    hdr.opcode = uint8_t(opcode);
    KMError ret = KMError::FAILED;
    if (extension_handler_ && !WS_FLAG_NO_COMPRESS(flags)) {
        ret = extension_handler_->handleOutcomingFrame(hdr, const_cast<KMBuffer&>(buf));
    } else {
        ret = sendWsFrame(hdr, buf);
    }
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
        WSError err = ws_handler_.handleData(src, len);
        DESTROY_DETECTOR_CHECK(KMError::DESTROYED);
        if(getState() == State::IN_ERROR || getState() == State::CLOSED) {
            return KMError::INVALID_STATE;
        }
        if(err != WSError::NOERR &&
           err != WSError::NEED_MORE_DATA) {
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
    
    auto extensionOffer = ExtensionHandler::getExtensionOffer();
    if (!extensionOffer.empty()) {
        ss << kSecWebSocketExtensions << ": " << extensionOffer << "\r\n";
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
        
        if (extension_handler_) {
            auto extensionAnswer = extension_handler_->getExtensionAnswer();
            if (!extensionAnswer.empty()) {
                ss << kSecWebSocketExtensions << ": " << extensionAnswer << "\r\n";
            }
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

KMError WebSocket::Impl::onWsFrame(FrameHeader hdr, KMBuffer &buf)
{
    if (hdr.rsv1 != 0 || hdr.rsv2 != 0 || hdr.rsv3 != 0) {
        setState(State::IN_ERROR);
        if(error_cb_) error_cb_(KMError::PROTO_ERROR);
        return KMError::PROTO_ERROR;
    }
    if (WSHandler::isControlFrame(hdr.opcode)) {
        auto buf_len = buf.chainLength();
        if ((uint8_t)WSOpcode::CLOSE == hdr.opcode) {
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
        } else if ((uint8_t)WSOpcode::PING == hdr.opcode) {
            sendPongFrame(buf);
        }
    } else {
        bool is_text = (uint8_t)WSOpcode::TEXT == hdr.opcode;
        if(data_cb_) data_cb_(buf, is_text, hdr.fin);
    }
    
    return KMError::NOERR;
}

void WebSocket::Impl::onWsHandshake(KMError err)
{
    handshake_result_ = err;
    if (isServer()) {
        origin_ = ws_handler_.getOrigin();
        subprotocol_ = ws_handler_.getSubprotocol();
        extensions_ = ws_handler_.getExtensions();
        if(handshake_cb_ && KMError::NOERR == err) {
            DESTROY_DETECTOR_SETUP();
            auto rv = handshake_cb_(err);
            DESTROY_DETECTOR_CHECK_VOID();
            if (!rv) {
                handshake_result_ = KMError::REJECTED;
            } else {
                negotiateExtensions();
            }
        }
        sendUpgradeResponse();
    } else {
        if(KMError::NOERR == err) {
            extensions_ = ws_handler_.getExtensions();
            negotiateExtensions();
            onStateOpen();
        } else {
            setState(State::IN_ERROR);
            if(error_cb_) error_cb_(err);
        }
    }
}

KMError WebSocket::Impl::negotiateExtensions()
{
    std::string ext_list = ws_handler_.getExtensions();
    
    if (!ext_list.empty()) {
        auto ext_handler = std::make_unique<ExtensionHandler>();
        auto err = ext_handler->negotiateExtensions(ext_list, ws_handler_.getMode() == WSMode::CLIENT);
        if (err != KMError::NOERR) {
            return err;
        }
        if (ext_handler->hasExtension()) {
            extension_handler_ = std::move(ext_handler);
            extension_handler_->setIncomingCallback([this] (FrameHeader hdr, KMBuffer &buf) {
                return onExtensionIncomingFrame(hdr, buf);
            });
            extension_handler_->setOutcomingCallback([this] (FrameHeader hdr, KMBuffer &buf) {
                return onExtensionOutcomingFrame(hdr, buf);
            });
            
            ws_handler_.setFrameCallback([this] (FrameHeader hdr, KMBuffer &buf) {
                if (WSHandler::isControlFrame(hdr.opcode)) {
                    return onWsFrame(hdr, buf);
                } else {
                    return extension_handler_->handleIncomingFrame(hdr, buf);
                }
            });
        }
    }
    
    return KMError::NOERR;
}

KMError WebSocket::Impl::onExtensionIncomingFrame(ws::FrameHeader hdr, KMBuffer &buf)
{
    return onWsFrame(hdr, buf);
}

KMError WebSocket::Impl::onExtensionOutcomingFrame(ws::FrameHeader hdr, KMBuffer &buf)
{
    return sendWsFrame(hdr, buf);
}

KMError WebSocket::Impl::sendWsFrame(ws::FrameHeader hdr, uint8_t *payload, size_t plen)
{
    uint8_t hdr_buf[WS_MAX_HEADER_SIZE];
    int hdr_len = 0;
    if (ws_handler_.getMode() == WSMode::CLIENT && plen > 0) {
        hdr.mask = 1;
        *(uint32_t*)hdr.maskey = generateMaskKey();
        WSHandler::handleDataMask(hdr.maskey, payload, plen);
    }
    hdr.length = uint32_t(plen);
    hdr_len = ws_handler_.encodeFrameHeader(hdr, hdr_buf);
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

KMError WebSocket::Impl::sendWsFrame(FrameHeader hdr, const KMBuffer &buf)
{
    size_t plen = buf.chainLength();
    uint8_t hdr_buf[WS_MAX_HEADER_SIZE];
    int hdr_len = 0;
    if (ws_handler_.getMode() == WSMode::CLIENT && plen > 0) {
        hdr.mask = 1;
        *(uint32_t*)hdr.maskey = generateMaskKey();
        WSHandler::handleDataMask(hdr.maskey, const_cast<KMBuffer&>(buf));
    }
    hdr.length = uint32_t(plen);
    hdr_len = ws_handler_.encodeFrameHeader(hdr, hdr_buf);
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
    FrameHeader hdr;
    memset(&hdr, 0, sizeof(hdr));
    hdr.fin = 1;
    hdr.opcode = uint8_t(WSOpcode::CLOSE);
    if (statusCode != 0) {
        uint8_t payload[2];
        payload[0] = statusCode >> 8;
        payload[1] = statusCode & 0xFF;
        return sendWsFrame(hdr, payload, 2);
    } else {
        return sendWsFrame(hdr, nullptr, 0);
    }
}

KMError WebSocket::Impl::sendPingFrame(const KMBuffer &buf)
{
    FrameHeader hdr;
    memset(&hdr, 0, sizeof(hdr));
    hdr.fin = 1;
    hdr.opcode = uint8_t(WSOpcode::PING);
    return sendWsFrame(hdr, buf);
}

KMError WebSocket::Impl::sendPongFrame(const KMBuffer &buf)
{
    FrameHeader hdr;
    memset(&hdr, 0, sizeof(hdr));
    hdr.fin = 1;
    hdr.opcode = uint8_t(WSOpcode::PONG);
    return sendWsFrame(hdr, buf);
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
