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
#include "libkev/src/utils/kmtrace.h"
#include "utils/base64.h"
#include "exts/ExtensionHandler.h"

using namespace kuma;
using namespace kuma::ws;


static std::string generate_sec_accept_value(const std::string& sec_ws_key);

WSConnection_V1::WSConnection_V1(const EventLoopPtr &loop)
: stream_(new H1xStream(loop))
{
    stream_->setHeaderCallback([this] () {
        onHeader();
    });
    stream_->setDataCallback([this] (KMBuffer &buf) {
        onData(buf);
    });
    stream_->setWriteCallback([this] (KMError err) {
        onWrite();
    });
    stream_->setErrorCallback([this] (KMError err) {
        onError(err);
    });
    stream_->setIncomingCompleteCallback([this] {
        if (stream_->isServer()) {
            handleUpgradeRequest();
        } else {
            handleUpgradeResponse();
        }
    });
    stream_->setOutgoingCompleteCallback([this] {
        //onError(KMError::PROTO_ERROR);
    });
    KM_SetObjKey("WSConnection_V1");
}

WSConnection_V1::~WSConnection_V1()
{
    
}

void WSConnection_V1::cleanup()
{
    stream_->close();
}

KMError WSConnection_V1::setProxyInfo(const ProxyInfo &proxy_info)
{
    return stream_->setProxyInfo(proxy_info);
}

KMError WSConnection_V1::addHeader(std::string name, std::string value)
{
    return stream_->addHeader(std::move(name), std::move(value));
}

KMError WSConnection_V1::addHeader(std::string name, uint32_t value)
{
    return addHeader(std::move(name), std::to_string(value));
}

KMError WSConnection_V1::connect(const std::string& ws_url)
{
    return connect_i(ws_url);
}

KMError WSConnection_V1::connect_i(const std::string& ws_url)
{
    Uri uri;
    if (!uri.parse(ws_url)) {
        return KMError::INVALID_PARAM;
    }
    auto pos = ws_url.find("://");
    if(pos == std::string::npos) {
        return KMError::INVALID_PARAM;
    }
    std::string http_url;
    if(kev::is_equal("wss", uri.getScheme())) {
        http_url = "https";
    } else {
        http_url = "http";
    }
    http_url.append(ws_url.begin()+pos, ws_url.end());
    
    addHeader(strUpgrade, "websocket");
    addHeader("Connection", "Upgrade");
    addHeader(strHost, uri.getHost());
    if (!origin_.empty()) {
        addHeader("Origin", origin_);
    }
    addHeader(kSecWebSocketKey, "dGhlIHNhbXBsZSBub25jZQ==");
    if (!subprotocol_.empty()) {
        addHeader(kSecWebSocketProtocol, subprotocol_);
    }
    if (!extensions_.empty()) {
        addHeader(kSecWebSocketExtensions, extensions_);
    }
    addHeader(kSecWebSocketVersion, kWebSocketVersion);
    
    setState(State::UPGRADING);
    auto ret = stream_->sendRequest("GET", http_url, "HTTP/1.1");
    if (ret != KMError::NOERR) {
        onStateError(ret);
    }
    return ret;
}

KMError WSConnection_V1::attachFd(SOCKET_FD fd, const KMBuffer *init_buf, HandshakeCallback cb)
{
    handshake_cb_ = std::move(cb);
    setState(State::UPGRADING);
    return stream_->attachFd(fd, init_buf);
}

KMError WSConnection_V1::attachSocket(TcpSocket::Impl&& tcp,
                                      HttpParser::Impl&& parser,
                                      const KMBuffer *init_buf,
                                      HandshakeCallback cb)
{
    handshake_cb_ = std::move(cb);
    setState(State::UPGRADING);
    
    return stream_->attachSocket(std::move(tcp), std::move(parser), init_buf);
}

int WSConnection_V1::send(const iovec* iovs, int count)
{
    if (count <= 0) {
        return 0;
    }
    KMBuffer bufs[8];
    KMBuffer *send_bufs = bufs;
    if (count > ARRAY_SIZE(bufs)) {
        send_bufs = new KMBuffer[count];
    }
    send_bufs[0].reset(iovs[0].iov_base, iovs[0].iov_len, iovs[0].iov_len);
    for (int i = 1; i < count; ++i) {
        send_bufs[i].reset(iovs[i].iov_base, iovs[i].iov_len, iovs[i].iov_len);
        send_bufs[0].append(&send_bufs[i]);
    }
    auto ret = stream_->sendData(send_bufs[0]);
    send_bufs[0].reset();
    if (send_bufs != bufs) {
        delete[] send_bufs;
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
    return stream_->canSendData();
}

KMError WSConnection_V1::sendUpgradeResponse(int status_code, const std::string &desc)
{
    if (status_code == 101) {
        addHeader(strUpgrade, "websocket");
        addHeader("Connection", "Upgrade");
        addHeader(kSecWebSocketAccept, generate_sec_accept_value(sec_ws_key_));
        if (!subprotocol_.empty()) {
            addHeader(kSecWebSocketProtocol, subprotocol_);
        }
        if (!extensions_.empty()) {
            addHeader(kSecWebSocketExtensions, extensions_);
        }
    }
    addHeader(kSecWebSocketVersion, kWebSocketVersion);
    
    auto ret = stream_->sendResponse(status_code, desc, "HTTP/1.1");
    if (ret == KMError::NOERR) {
        if (status_code == 101) {
            setState(State::OPEN);
            onStateOpen();
        } else {
            setState(State::IN_ERROR);
        }
    }
    return ret;
}

void WSConnection_V1::handleUpgradeRequest()
{
    auto err = KMError::NOERR;
    auto const &req_header = stream_->getIncomingHeaders();
    origin_ = req_header.getHeader("Origin");
    do {
        if (!kev::is_equal(req_header.getHeader("Upgrade"), "WebSocket") ||
            !kev::contains_token(req_header.getHeader("Connection"), "Upgrade", ',')) {
            KM_ERRXTRACE("handleRequest, not WebSocket request");
            err = KMError::PROTO_ERROR;
            break;
        }
        
        auto const &sec_ws_ver = req_header.getHeader(kSecWebSocketVersion);
        if (sec_ws_ver.empty() || !kev::contains_token(sec_ws_ver, kWebSocketVersion, ',')) {
            KM_ERRXTRACE("handleRequest, unsupported version number, ver="<<sec_ws_ver);
            err = KMError::PROTO_ERROR;
            break;
        }
        auto const &sec_ws_key = req_header.getHeader(kSecWebSocketKey);
        if(sec_ws_key.empty()) {
            KM_ERRXTRACE("handleRequest, no Sec-WebSocket-Key");
            err = KMError::PROTO_ERROR;
            break;
        }
        sec_ws_key_ = sec_ws_key;
    } while (false);
    
    if(handshake_cb_) {
        checkHandshake();
        DESTROY_DETECTOR_SETUP();
        auto rv = handshake_cb_(err);
        DESTROY_DETECTOR_CHECK_VOID();
        if (err == KMError::NOERR && getState() == State::UPGRADING) {
            if (!rv) {
                err = KMError::REJECTED;
            }
            int status_code = 101;
            std::string desc = "Switching Protocols";
            if (err == KMError::REJECTED) {
                status_code = 403;
                desc = "Forbidden";
            } else if (err != KMError::NOERR) {
                status_code = 400;
                desc = "Bad Request";
            }
            err = sendUpgradeResponse(status_code, desc);
            if (err != KMError::NOERR) {
                onStateError(err);
            }
        }
    }
}

void WSConnection_V1::handleUpgradeResponse()
{
    auto err = KMError::NOERR;
    int status_code = stream_->getStatusCode();
    auto const &rsp_header = stream_->getIncomingHeaders();
    if (status_code != 101 ||
        !kev::is_equal(rsp_header.getHeader("Upgrade"), "WebSocket") ||
        !kev::contains_token(rsp_header.getHeader("Connection"), "Upgrade", ',')) {
        KM_ERRXTRACE("handleUpgradeResponse, invalid status code: "<<status_code);
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
        if (kev::is_equal(kv.first, kSecWebSocketProtocol)) {
            if (subprotocol_.empty()) {
                subprotocol_ = kv.second;
            } else {
                subprotocol_ += ", " + kv.second;
            }
        } else if (kev::is_equal(kv.first, kSecWebSocketExtensions)) {
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

void WSConnection_V1::onWrite()
{
    if (write_cb_) write_cb_(KMError::NOERR);
}

void WSConnection_V1::onError(KMError err)
{
    //KM_INFOXTRACE("onError, err="<<int(err));
    onStateError(err);
}

void WSConnection_V1::onHeader()
{
    
}

void WSConnection_V1::onData(KMBuffer &buf)
{
    if (data_cb_) {
        data_cb_(buf);
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
    
    return x64_encode(uShaRst2, SHA1_DIGEST_SIZE, false);
}

