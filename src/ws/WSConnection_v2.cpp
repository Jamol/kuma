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


#include "WSConnection_v2.h"
#include "http/v2/H2StreamProxy.h"
#include "libkev/src/utils/kmtrace.h"

using namespace kuma;
using namespace kuma::ws;

const std::string H2HeaderProtocol {":protocol"};
const std::string kWsMethod {"CONNECT"};
const std::string kWsProtocol {"websocket"};
const std::string kSecWsVersion { "sec-websocket-version" };
const std::string kSecWsProtocol { "sec-websocket-protocol" };
const std::string kSecWsExtensions { "sec-websocket-extensions" };

WSConnection_V2::WSConnection_V2(const EventLoopPtr &loop)
: stream_(new H2StreamProxy(loop))
{
    stream_->setHeaderCallback([this] {
        onHeader();
    });
    stream_->setDataCallback([this] (KMBuffer &buf) {
        onData(buf);
    });
    stream_->setErrorCallback([this] (KMError err) {
        onError(err);
    });
    stream_->setWriteCallback([this] (KMError err) {
        onWrite();
    });
    stream_->setIncomingCompleteCallback([this] {
        onError(KMError::PROTO_ERROR);
    });
    stream_->setOutgoingCompleteCallback([this] {
        onError(KMError::PROTO_ERROR);
    });
    KM_SetObjKey("WSConnection_V2");
}

WSConnection_V2::~WSConnection_V2()
{
    
}

KMError WSConnection_V2::setSslFlags(uint32_t ssl_flags)
{
    ssl_flags_ = ssl_flags;
    return KMError::NOERR;
}

KMError WSConnection_V2::setProxyInfo(const ProxyInfo &proxy_info)
{
    return stream_->setProxyInfo(proxy_info);
}

KMError WSConnection_V2::addHeader(std::string name, std::string value)
{
    return stream_->addHeader(std::move(name), std::move(value));
}

KMError WSConnection_V2::addHeader(std::string name, uint32_t value)
{
    return stream_->addHeader(std::move(name), std::to_string(value));
}

const std::string& WSConnection_V2::getPath() const
{
    return stream_->getPath();
}

const HttpHeader& WSConnection_V2::getHeaders() const
{
    return stream_->getIncomingHeaders();
}

KMError WSConnection_V2::connect(const std::string& ws_url)
{
    addHeader(H2HeaderProtocol, kWsProtocol);
    if (!origin_.empty()) {
        addHeader(strHost, origin_);
    }
    if (!subprotocol_.empty()) {
        addHeader(kSecWsProtocol, subprotocol_);
    }
    if (!extensions_.empty()) {
        addHeader(kSecWsExtensions, extensions_);
    }
    addHeader(kSecWsVersion, kWebSocketVersion);
    setState(State::UPGRADING);
    return stream_->sendRequest(kWsMethod, ws_url, ssl_flags_);
}

KMError WSConnection_V2::attachStream(uint32_t stream_id, const H2ConnectionPtr& conn, HandshakeCallback cb)
{
    handshake_cb_ = std::move(cb);
    setState(State::UPGRADING);
    return stream_->attachStream(stream_id, conn);
}

KMError WSConnection_V2::sendResponse(int status_code)
{
    if (!subprotocol_.empty()) {
        addHeader(kSecWsProtocol, subprotocol_);
    }
    if (!extensions_.empty()) {
        addHeader(kSecWsExtensions, extensions_);
    }
    addHeader(kSecWsVersion, kWebSocketVersion);
    auto ret = stream_->sendResponse(status_code);
    if (ret == KMError::NOERR) {
        if (status_code == 200) {
            setState(State::OPEN);
            stream_->runOnLoopThread([this] { onStateOpen(); });
        } else {
            setState(State::IN_ERROR);
        }
    }
    return ret;
}

int WSConnection_V2::send(const iovec* iovs, int count)
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

KMError WSConnection_V2::close()
{
    stream_->close();
    setState(State::CLOSED);
    return KMError::NOERR;
}

bool WSConnection_V2::canSendData() const
{
    return stream_->canSendData();
}

void WSConnection_V2::handleHandshakeRequest()
{
    auto const &req_header = getHeaders();
    std::string method = stream_->getMethod();
    std::string protocol = req_header.getHeader(H2HeaderProtocol);
    origin_ = req_header.getHeader("origin");
    
    auto err = KMError::NOERR;
    if (method != kWsMethod || protocol != kWsProtocol) {
        err = KMError::PROTO_ERROR;
    }
    if (handshake_cb_) {
        checkHandshake();
        DESTROY_DETECTOR_SETUP();
        auto rv = handshake_cb_(err);
        DESTROY_DETECTOR_CHECK_VOID();
        if (err == KMError::NOERR && getState() == State::UPGRADING) {
            if (!rv) {
                err = KMError::REJECTED;
            }
            int status_code = 200;
            if (err == KMError::REJECTED) {
                status_code = 403;
            } else if (err != KMError::NOERR) {
                status_code = 400;
            }
            err = sendResponse(status_code);
            if (err != KMError::NOERR) {
                onStateError(err);
            }
        }
    }
}

void WSConnection_V2::handleHandshakeResponse()
{
    auto err = KMError::NOERR;
    int status_code = stream_->getStatusCode();
    if (status_code != 200) {
        err = KMError::PROTO_ERROR;
        KM_ERRXTRACE("handleHandshakeResponse, invalid status code: "<<status_code);
    }
    
    if (err == KMError::NOERR) {
        checkHandshake();
        onStateOpen();
    } else {
        onStateError(err);
    }
}

void WSConnection_V2::checkHandshake()
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

void WSConnection_V2::onError(KMError err)
{
    onStateError(err);
}

void WSConnection_V2::onHeader()
{
    if (stream_->isServer()) {
        handleHandshakeRequest();
    } else {
        handleHandshakeResponse();
    }
}

void WSConnection_V2::onData(KMBuffer &buf)
{
    if (data_cb_)  data_cb_(buf);
}

void WSConnection_V2::onWrite()
{
    if (write_cb_) write_cb_(KMError::NOERR);
}
