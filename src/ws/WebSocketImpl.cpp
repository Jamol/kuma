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
#include "exts/ExtensionHandler.h"
#include "WSConnection_v1.h"
#include "WSConnection_v2.h"

#include <sstream>

using namespace kuma;
using namespace kuma::ws;

#define WS_FLAG_NO_COMPRESS(flags) (flags & 0x01)

//////////////////////////////////////////////////////////////////////////
WebSocket::Impl::Impl(const EventLoopPtr &loop, const std::string &http_ver)
{
    if (is_equal(http_ver, "HTTP/2.0")) {
        ws_conn_.reset(new WSConnection_V2(loop));
    } else {
        ws_conn_.reset(new WSConnection_V1(loop));
    }
    ws_conn_->setOpenCallback([this] (KMError err) {
        onWsOpen(err);
    });
    ws_conn_->setDataCallback([this] (KMBuffer &buf) {
        onWsData(buf);
    });
    ws_conn_->setWriteCallback([this] (KMError err) {
        onWsWrite();
    });
    ws_conn_->setErrorCallback([this] (KMError err) {
        onWsError(err);
    });
    
    ws_handler_.setFrameCallback([this] (ws::FrameHeader hdr, KMBuffer &buf) {
        return onWsFrame(hdr, buf);
    });
    
    KM_SetObjKey("WebSocket");
}

WebSocket::Impl::~Impl()
{
    
}

void WebSocket::Impl::cleanup()
{
    ws_conn_->close();
    
    ws_handler_.reset();
    body_bytes_sent_ = 0;
    fragmented_ = false;
    extension_handler_.reset();
}

bool WebSocket::Impl::isServer() const
{
    return ws_handler_.getMode() == WSMode::SERVER;
}

KMError WebSocket::Impl::setSslFlags(uint32_t ssl_flags)
{
    return ws_conn_->setSslFlags(ssl_flags);
}

KMError WebSocket::Impl::setProxyInfo(const ProxyInfo &proxy_info)
{
    return ws_conn_->setProxyInfo(proxy_info);
}

KMError WebSocket::Impl::connect(const std::string& ws_url)
{
    if(getState() != State::IDLE && getState() != State::CLOSED) {
        KUMA_ERRXTRACE("connect, invalid state, state="<<getState());
        return KMError::INVALID_STATE;
    }
    ws_handler_.setMode(WSMode::CLIENT);
    setState(State::HANDSHAKE);
    auto extensions = ExtensionHandler::getExtensionOffer();
    ws_conn_->setExtensions(extensions);
    return ws_conn_->connect(ws_url);
}

KMError WebSocket::Impl::attachFd(SOCKET_FD fd, const KMBuffer *init_buf, HandshakeCallback cb)
{
    auto *ws_conn_v1 = dynamic_cast<WSConnection_V1*>(ws_conn_.get());
    if (!ws_conn_v1) {
        return KMError::FAILED;
    }
    handshake_cb_ = std::move(cb);
    ws_handler_.setMode(WSMode::SERVER);
    setState(State::HANDSHAKE);
    return ws_conn_v1->attachFd(fd, init_buf, [this] (KMError err) {
        return onWsHandshake(err);
    });
}

KMError WebSocket::Impl::attachSocket(TcpSocket::Impl&& tcp, HttpParser::Impl&& parser, const KMBuffer *init_buf, HandshakeCallback cb)
{
    auto *ws_conn_v1 = dynamic_cast<WSConnection_V1*>(ws_conn_.get());
    if (!ws_conn_v1) {
        return KMError::FAILED;
    }
    handshake_cb_ = std::move(cb);
    ws_handler_.setMode(WSMode::SERVER);
    setState(State::HANDSHAKE);

    return ws_conn_v1->attachSocket(std::move(tcp), std::move(parser), init_buf, [this] (KMError err) {
        return onWsHandshake(err);
    });
}

KMError WebSocket::Impl::attachStream(uint32_t stream_id, H2Connection::Impl* conn, HandshakeCallback cb)
{
    auto *ws_conn_v2 = dynamic_cast<WSConnection_V2*>(ws_conn_.get());
    if (!ws_conn_v2) {
        return KMError::FAILED;
    }
    handshake_cb_ = std::move(cb);
    ws_handler_.setMode(WSMode::SERVER);
    setState(State::HANDSHAKE);
    
    return ws_conn_v2->attachStream(stream_id, conn, [this] (KMError err) {
        return onWsHandshake(err);
    });
}

int WebSocket::Impl::send(const void* data, size_t len, bool is_text, bool is_fin, uint32_t flags)
{
    if(getState() != State::OPEN) {
        return -1;
    }
    if(!ws_conn_->canSendData()) {
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
    
    ws::FrameHeader hdr;
    memset(&hdr, 0, sizeof(hdr));
    hdr.fin = is_fin ? 1 : 0;
    hdr.opcode = uint8_t(opcode);
    if (extension_handler_ && !WS_FLAG_NO_COMPRESS(flags)) {
        KMBuffer buf(data, len, len);
        ret = extension_handler_->handleOutgoingFrame(hdr, buf);
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
    if(!ws_conn_->canSendData()) {
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
    
    ws::FrameHeader hdr;
    memset(&hdr, 0, sizeof(hdr));
    hdr.fin = is_fin ? 1 : 0;
    hdr.opcode = uint8_t(opcode);
    KMError ret = KMError::FAILED;
    if (extension_handler_ && !WS_FLAG_NO_COMPRESS(flags)) {
        ret = extension_handler_->handleOutgoingFrame(hdr, const_cast<KMBuffer&>(buf));
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

void WebSocket::Impl::onWsData(KMBuffer &buf)
{
    if (getState() == State::OPEN) {
        for (auto it = buf.begin(); it != buf.end(); ++it) {
            auto *data = static_cast<uint8_t*>(it->readPtr());
            auto len = it->length();
            DESTROY_DETECTOR_SETUP();
            WSError err = ws_handler_.handleData(data, len);
            DESTROY_DETECTOR_CHECK_VOID();
            if(getState() == State::IN_ERROR || getState() == State::CLOSED) {
                return ;
            }
            if(err != WSError::NOERR &&
               err != WSError::NEED_MORE_DATA) {
                onError(KMError::FAILED);
                return ;
            }
        }
    } else {
        KUMA_WARNXTRACE("onWsData, invalid state: "<<getState());
    }
}

void WebSocket::Impl::onWsWrite()
{
    if (write_cb_) write_cb_(KMError::NOERR);
}

void WebSocket::Impl::onError(KMError err)
{
    KUMA_ERRXTRACE("onError, err="<<int(err));
    cleanup();
    setState(State::IN_ERROR);
    if(error_cb_) error_cb_(err);
}

void WebSocket::Impl::onStateOpen()
{
    KUMA_INFOXTRACE("onStateOpen");
    setState(State::OPEN);
    if (open_cb_) open_cb_(KMError::NOERR);
}

KMError WebSocket::Impl::onWsFrame(ws::FrameHeader hdr, KMBuffer &buf)
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

bool WebSocket::Impl::onWsHandshake(KMError err)
{
    if(handshake_cb_ && KMError::NOERR == err) {
        DESTROY_DETECTOR_SETUP();
        auto rv = handshake_cb_(err);
        DESTROY_DETECTOR_CHECK(false);
        if (rv) {
            negotiateExtensions();
            if (extension_handler_) {
                auto extensions = extension_handler_->getExtensionAnswer();
                ws_conn_->setExtensions(extensions);
            }
        }
        return rv;
    }
    
    onError(err);
    return false;
}

void WebSocket::Impl::onWsOpen(KMError err)
{
    if(KMError::NOERR == err) {
        if (!isServer()) {
            negotiateExtensions();
        }
        onStateOpen();
    } else {
        onError(err);
    }
}

void WebSocket::Impl::onWsError(KMError err)
{
    onError(err);
}

KMError WebSocket::Impl::negotiateExtensions()
{
    auto const &extensions = ws_conn_->getExtensions();
    if (!extensions.empty()) {
        auto ext_handler = std::make_unique<ExtensionHandler>();
        auto err = ext_handler->negotiateExtensions(extensions, ws_handler_.getMode() == WSMode::CLIENT);
        if (err != KMError::NOERR) {
            return err;
        }
        if (ext_handler->hasExtension()) {
            extension_handler_ = std::move(ext_handler);
            extension_handler_->setIncomingCallback([this] (ws::FrameHeader hdr, KMBuffer &buf) {
                return onExtensionIncomingFrame(hdr, buf);
            });
            extension_handler_->setOutgoingCallback([this] (ws::FrameHeader hdr, KMBuffer &buf) {
                return onExtensionOutgoingFrame(hdr, buf);
            });
            
            ws_handler_.setFrameCallback([this] (ws::FrameHeader hdr, KMBuffer &buf) {
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

KMError WebSocket::Impl::onExtensionOutgoingFrame(ws::FrameHeader hdr, KMBuffer &buf)
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
    auto ret = ws_conn_->send(iovs, cnt);
    return ret < 0 ? KMError::SOCK_ERROR : KMError::NOERR;
}

KMError WebSocket::Impl::sendWsFrame(ws::FrameHeader hdr, const KMBuffer &buf)
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
    
    iovec iovs[129];
    int count = 0;
    iovs[count].iov_base = (char*)hdr_buf;
    iovs[count++].iov_len = hdr_len;
    for (auto it = buf.begin(); it != buf.end(); ++it) {
        if (it->length() > 0) {
            if (count < 129) {
                iovs[count].iov_base = static_cast<char*>(it->readPtr());
                iovs[count++].iov_len = static_cast<decltype(iovs[0].iov_len)>(it->length());
            } else {
                return KMError::BUFFER_TOO_LONG;
            }
        }
    }
    
    auto ret = ws_conn_->send(iovs, count);
    return ret < 0 ? KMError::SOCK_ERROR : KMError::NOERR;
}

KMError WebSocket::Impl::sendCloseFrame(uint16_t statusCode)
{
    ws::FrameHeader hdr;
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
    ws::FrameHeader hdr;
    memset(&hdr, 0, sizeof(hdr));
    hdr.fin = 1;
    hdr.opcode = uint8_t(WSOpcode::PING);
    return sendWsFrame(hdr, buf);
}

KMError WebSocket::Impl::sendPongFrame(const KMBuffer &buf)
{
    ws::FrameHeader hdr;
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
