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


#include "WSConnection_v1.h"
#include "util/kmtrace.h"
#include "util/base64.h"
#include "exts/ExtensionHandler.h"

using namespace kuma;
using namespace kuma::ws;


static std::string generate_sec_accept_value(const std::string& sec_ws_key);

WSConnection_V1::WSConnection_V1(const EventLoopPtr &loop)
: TcpConnection(loop)
{
    loop_token_.eventLoop(loop);
    KM_SetObjKey("WSConnection_V1");
}

WSConnection_V1::~WSConnection_V1()
{
    
}

void WSConnection_V1::cleanup()
{
    TcpConnection::close();
    loop_token_.reset();
    
    http_parser_.reset();
}

KMError WSConnection_V1::addHeader(std::string name, std::string value)
{
    return outgoing_header_.addHeader(std::move(name), std::move(value));
}

KMError WSConnection_V1::addHeader(std::string name, uint32_t value)
{
    return outgoing_header_.addHeader(std::move(name), value);
}

KMError WSConnection_V1::connect(const std::string& ws_url)
{
    return connect_i(ws_url);
}

KMError WSConnection_V1::connect_i(const std::string& ws_url)
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

KMError WSConnection_V1::attachFd(SOCKET_FD fd, const KMBuffer *init_buf, HandshakeCallback cb)
{
    handshake_cb_ = std::move(cb);
    setState(State::UPGRADING);
    return TcpConnection::attachFd(fd, init_buf);
}

KMError WSConnection_V1::attachSocket(TcpSocket::Impl&& tcp,
                                      HttpParser::Impl&& parser,
                                      const KMBuffer *init_buf,
                                      HandshakeCallback cb)
{
    handshake_cb_ = std::move(cb);
    setState(State::UPGRADING);
    
    http_parser_.reset();
    http_parser_ = std::move(parser);
    http_parser_.setDataCallback([this] (KMBuffer &buf) { onHttpData(buf); });
    http_parser_.setEventCallback([this] (HttpEvent ev) { onHttpEvent(ev); });

    auto ret = TcpConnection::attachSocket(std::move(tcp), init_buf);
    if(http_parser_.paused()) {
        http_parser_.resume();
    }
    return ret;
}

KMError WSConnection_V1::close()
{
    cleanup();
    setState(State::CLOSED);
    return KMError::NOERR;
}

bool WSConnection_V1::canSendData() const
{
    return TcpConnection::sendBufferEmpty();
}

std::string WSConnection_V1::buildUpgradeRequest(const std::string &origin,
                                                 const std::string &subprotocol,
                                                 const std::string &extensions,
                                                 const HeaderVector &header_vec)
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
    if (!origin.empty()) {
        ss << "Origin: " << origin << "\r\n";
    }
    ss << kSecWebSocketKey << ": " << "dGhlIHNhbXBsZSBub25jZQ==" << "\r\n";
    if (!subprotocol.empty()) {
        ss << kSecWebSocketProtocol << ": " << subprotocol << "\r\n";
    }
    
    if (!extensions.empty()) {
        ss << kSecWebSocketExtensions << ": " << extensions << "\r\n";
    }
    
    ss << kSecWebSocketVersion << ": " << kWebSocketVersion << "\r\n";
    
    for (auto &kv : header_vec) {
        ss << kv.first << ": " << kv.second << "\r\n";
    }
    
    ss << "\r\n";
    return ss.str();
}

std::string WSConnection_V1::buildUpgradeResponse(KMError result,
                                                  const std::string &subprotocol,
                                                  const std::string &extensions,
                                                  const HeaderVector &header_vec)
{
    if (result == KMError::NOERR) {
        std::stringstream ss;
        ss << "HTTP/1.1 101 Switching Protocols\r\n";
        ss << "Upgrade: websocket\r\n";
        ss << "Connection: Upgrade\r\n";
        ss << kSecWebSocketAccept << ": " << generate_sec_accept_value(sec_ws_key_) << "\r\n";
        if(!subprotocol.empty()) {
            ss << kSecWebSocketProtocol << ": " << subprotocol << "\r\n";
        }
        
        if (!extensions.empty()) {
            ss << kSecWebSocketExtensions << ": " << extensions << "\r\n";
        }
        
        for (auto &kv : header_vec) {
            ss << kv.first << ": " << kv.second << "\r\n";
        }
        
        ss << "\r\n";
        return ss.str();
    } else {
        std::stringstream ss;
        if (result == KMError::REJECTED) {
            ss << "HTTP/1.1 403 Forbidden\r\n";
        } else {
            ss << "HTTP/1.1 400 Bad Request\r\n";
        }
        ss << kSecWebSocketVersion << ": " << kWebSocketVersion << "\r\n";
        
        for (auto &kv : header_vec) {
            ss << kv.first << ": " << kv.second << "\r\n";
        }
        
        ss << "\r\n";
        return ss.str();
    }
}

KMError WSConnection_V1::sendUpgradeRequest(const std::string &origin,
                                            const std::string &subprotocol,
                                            const std::string &extensions,
                                            const HeaderVector &header_vec)
{
    std::string str(buildUpgradeRequest(origin, subprotocol, extensions, header_vec));
    setState(State::UPGRADING);
    if (TcpConnection::send((const uint8_t*)str.c_str(), str.size()) > 0) {
        return KMError::NOERR;
    } else {
        return KMError::FAILED;
    }
}

KMError WSConnection_V1::sendUpgradeResponse(KMError result,
                                             const std::string &subprotocol,
                                             const std::string &extensions,
                                             const HeaderVector &header_vec)
{
    handshake_result_ = result;
    std::string str(buildUpgradeResponse(result, subprotocol, extensions, header_vec));
    setState(State::UPGRADING);
    int ret = TcpConnection::send((const uint8_t*)str.c_str(), str.size());
    if (ret == (int)str.size()) {
        if (handshake_result_ == KMError::NOERR) {
            setState(State::OPEN);
            eventLoop()->post([this] { onStateOpen(); }, &loop_token_);
        } else {
            setState(State::IN_ERROR);
        }
    }
    if (ret > 0) {
        return KMError::NOERR;
    } else {
        return KMError::FAILED;
    }
}

void WSConnection_V1::handleUpgradeRequest()
{
    auto err = KMError::NOERR;
    do {
        if(!http_parser_.isUpgradeTo("WebSocket")) {
            KUMA_ERRXTRACE("handleRequest, not WebSocket request");
            err = KMError::PROTO_ERROR;
            break;
        }
        auto const &sec_ws_ver = http_parser_.getHeaderValue(kSecWebSocketVersion);
        if (sec_ws_ver.empty() || !contains_token(sec_ws_ver, kWebSocketVersion, ',')) {
            KUMA_ERRXTRACE("handleRequest, unsupported version number, ver="<<sec_ws_ver);
            err = KMError::PROTO_ERROR;
            break;
        }
        auto const &sec_ws_key = http_parser_.getHeaderValue(kSecWebSocketKey);
        if(sec_ws_key.empty()) {
            KUMA_ERRXTRACE("handleRequest, no Sec-WebSocket-Key");
            err = KMError::PROTO_ERROR;
            break;
        }
        sec_ws_key_ = sec_ws_key;
    } while (0);
    
    origin_ = http_parser_.getHeader("Origin");
    checkHandshake();
    if(handshake_cb_) {
        DESTROY_DETECTOR_SETUP();
        auto rv = handshake_cb_(err);
        DESTROY_DETECTOR_CHECK_VOID();
        if (err == KMError::NOERR && getState() == State::UPGRADING) {
            if (!rv) {
                err = KMError::REJECTED;
            }
            err = sendUpgradeResponse(err, subprotocol_, extensions_, outgoing_header_.getHeaders());
            if (err != KMError::NOERR) {
                onStateError(err);
            }
        }
    }
}

void WSConnection_V1::handleUpgradeResponse()
{
    auto err = KMError::NOERR;
    if(!http_parser_.isUpgradeTo("WebSocket")) {
        KUMA_ERRXTRACE("handleResponse, invalid status code: "<<http_parser_.getStatusCode());
        err = KMError::PROTO_ERROR;
    }
    
    if (err == KMError::NOERR) {
        checkHandshake();
        onStateOpen();
    } else {
        onStateError(err);
    }
}

void WSConnection_V1::checkHandshake()
{
    subprotocol_.clear();
    extensions_.clear();
    auto const &incoming_header = getHeaders();
    for (auto const &kv : incoming_header.getHeaders()) {
        if (is_equal(kv.first, kSecWebSocketProtocol)) {
            if (subprotocol_.empty()) {
                subprotocol_ = kv.second;
            } else {
                subprotocol_ += ", " + kv.second;
            }
        } else if (is_equal(kv.first, kSecWebSocketExtensions)) {
            if (extensions_.empty()) {
                extensions_ = kv.second;
            } else {
                extensions_ += ", " + kv.second;
            }
        }
    }
}

void WSConnection_V1::onStateError(KMError err)
{
    cleanup();
    WSConnection::onStateError(err);
}

KMError WSConnection_V1::handleInputData(uint8_t *src, size_t len)
{
    if (getState() == State::OPEN) {
        if (data_cb_) {
            DESTROY_DETECTOR_SETUP();
            KMBuffer buf(src, len, len);
            data_cb_(buf);
            DESTROY_DETECTOR_CHECK(KMError::DESTROYED);
        }
    } else if (getState() == State::UPGRADING) {
        DESTROY_DETECTOR_SETUP();
        int bytes_used = http_parser_.parse((char*)src, len);
        DESTROY_DETECTOR_CHECK(KMError::DESTROYED);
        if(getState() == State::IN_ERROR || getState() == State::CLOSED) {
            return KMError::INVALID_STATE;
        }
        if(bytes_used < (int)len && getState() == State::OPEN && data_cb_) {
            KMBuffer buf(src + bytes_used, len - bytes_used, len - bytes_used);
            data_cb_(buf);
        }
    } else {
        KUMA_WARNXTRACE("handleInputData, invalid state: "<<getState());
    }
    return KMError::NOERR;
}

void WSConnection_V1::onConnect(KMError err)
{
    body_bytes_sent_ = 0;
    if (err == KMError::NOERR) {
        err = sendUpgradeRequest(origin_, subprotocol_, extensions_, outgoing_header_.getHeaders());
        if (err != KMError::NOERR) {
            onStateError(err);
        }
    } else {
        onStateError(err);
    }
}

void WSConnection_V1::onWrite()
{
    if(getState() == State::UPGRADING) {
        if (isServer()) {
            // response is sent out
            if (handshake_result_ == KMError::NOERR) {
                onStateOpen();
            } else {
                setState(State::IN_ERROR);
            }
            return;
        } else {
            return; // wait upgrade response on client side
        }
    }
    if (write_cb_) write_cb_(KMError::NOERR);
}

void WSConnection_V1::onError(KMError err)
{
    //KUMA_INFOXTRACE("onError, err="<<int(err));
    onStateError(err);
}

void WSConnection_V1::onHttpData(KMBuffer &buf)
{
    KUMA_ERRXTRACE("onHttpData, len="<<buf.chainLength());
}

void WSConnection_V1::onHttpEvent(HttpEvent ev)
{
    KUMA_INFOXTRACE("onHttpEvent, ev="<<int(ev));
    switch (ev) {
        case HttpEvent::HEADER_COMPLETE:
            break;
            
        case HttpEvent::COMPLETE:
            if(http_parser_.isRequest()) {
                handleUpgradeRequest();
            } else {
                handleUpgradeResponse();
            }
            break;
            
        case HttpEvent::HTTP_ERROR:
            setState(State::IN_ERROR);
            break;
            
        default:
            break;
    }
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

