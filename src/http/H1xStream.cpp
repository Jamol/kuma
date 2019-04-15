/* Copyright (c) 2016-2019, Fengping Bao <jamol@live.com>
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

#include "H1xStream.h"
#include "util/kmtrace.h"


using namespace kuma;

H1xStream::H1xStream(const EventLoopPtr &loop)
: TcpConnection(loop)
{
    loop_token_.eventLoop(loop);
    
    outgoing_message_.setSender([this] (const void* data, size_t len) -> int {
        return TcpConnection::send(data, len);
    });
    outgoing_message_.setVSender([this] (const iovec* iovs, int count) -> int {
        return TcpConnection::send(iovs, count);
    });
    outgoing_message_.setBSender([this] (const KMBuffer &buf) -> int {
        return TcpConnection::send(buf);
    });
    incoming_parser_.setDataCallback([this] (KMBuffer &buf) { onHttpData(buf); });
    incoming_parser_.setEventCallback([this] (HttpEvent ev) { onHttpEvent(ev); });
    KM_SetObjKey("H1xStream");
}

H1xStream::~H1xStream()
{
    loop_token_.reset();
}

KMError H1xStream::addHeader(std::string name, std::string value)
{
    return outgoing_message_.addHeader(std::move(name), std::move(value));
}

KMError H1xStream::sendRequest(const std::string &method, const std::string &url, const std::string &ver)
{
    if(!uri_.parse(url)) {
        return KMError::INVALID_PARAM;
    }
    method_ = method;
    version_ = ver;
    wait_outgoing_complete_ = false;
    if (!TcpConnection::isOpen()) {
        std::string str_port = uri_.getPort();
        uint16_t port = 80;
        uint32_t ssl_flags = SSL_NONE;
        if(is_equal("https", uri_.getScheme())) {
            port = 443;
            ssl_flags = SSL_ENABLE | getSslFlags();
        }
        if(!str_port.empty()) {
            port = std::stoi(str_port);
        }
        TcpConnection::setSslFlags(ssl_flags);
        return TcpConnection::connect(uri_.getHost().c_str(), port);
    } else {
        auto req = buildRequest();
        return sendHeaders(req);
    }
}

KMError H1xStream::attachFd(SOCKET_FD fd, const KMBuffer *init_buf)
{
    wait_outgoing_complete_ = false;
    return TcpConnection::attachFd(fd, init_buf);
}

KMError H1xStream::attachSocket(TcpSocket::Impl&& tcp, HttpParser::Impl&& parser, const KMBuffer *init_buf)
{
    bool is_parser_paused = parser.paused();
    bool is_parser_header_complete = parser.headerComplete();
    bool is_parser_complete = parser.complete();
    incoming_parser_.reset();
    incoming_parser_ = std::move(parser);
    incoming_parser_.setDataCallback([this] (KMBuffer &buf) { onHttpData(buf); });
    incoming_parser_.setEventCallback([this] (HttpEvent ev) { onHttpEvent(ev); });
    
    if (incoming_parser_.paused()) {
        incoming_parser_.resume();
    }
    
    wait_outgoing_complete_ = false;
    auto ret = TcpConnection::attachSocket(std::move(tcp), init_buf);
    if(ret == KMError::NOERR && is_parser_paused) {
        if (is_parser_header_complete) {
            DESTROY_DETECTOR_SETUP();
            onHeaderComplete();
            DESTROY_DETECTOR_CHECK(KMError::DESTROYED);
        }
        if (is_parser_complete) {
            onIncomingComplete();
        }
    }
    return ret;
}

KMError H1xStream::sendResponse(int status_code, const std::string &desc, const std::string &ver)
{
    auto rsp = buildResponse(status_code, desc, ver);
    return sendHeaders(rsp);
}

std::string H1xStream::buildRequest()
{
    std::stringstream ss;
    ss << uri_.getPath();
    if(!uri_.getQuery().empty()) {
        ss << "?" << uri_.getQuery();
    }
    if(!uri_.getFragment().empty()) {
        ss << "#" << uri_.getFragment();
    }
    auto url(ss.str());
    auto req = outgoing_message_.buildHeader(method_, url, version_);
    return req;
}

std::string H1xStream::buildResponse(int status_code, const std::string &desc, const std::string &ver)
{
    auto rsp = outgoing_message_.buildHeader(status_code, desc, ver, incoming_parser_.getMethod());
    return rsp;
}

KMError H1xStream::sendHeaders(const std::string &headers)
{
    auto ret = TcpConnection::send(headers.data(), headers.size());
    if (ret > 0) {
        if (outgoing_message_.isComplete()) {
            if (sendBufferEmpty()) {
                if (isServer()) {
                    runOnLoopThread([this] { onOutgoingComplete(); }, false);
                } else {
                    onOutgoingComplete();
                }
            } else {
                wait_outgoing_complete_ = true;
            }
        } else if (sendBufferEmpty()) {
            runOnLoopThread([this] { onWrite(); }, false);
        }
        return KMError::NOERR;
    } else if (ret < 0) {
        return KMError::SOCK_ERROR;
    } else {
        return KMError::FAILED;
    }
}

int H1xStream::sendData(const void* data, size_t len)
{
    auto ret = outgoing_message_.sendData(data, len);
    if (ret >= 0) {
        if (outgoing_message_.isComplete()) {
            if (sendBufferEmpty()) {
                if (isServer()) {
                    runOnLoopThread([this] { onOutgoingComplete(); }, false);
                } else {
                    onOutgoingComplete();
                }
            } else {
                wait_outgoing_complete_ = true;
            }
        }
    }
    return ret;
}

int H1xStream::sendData(const KMBuffer &buf)
{
    auto ret = outgoing_message_.sendData(buf);
    if (ret >= 0) {
        if (outgoing_message_.isComplete()) {
            if (sendBufferEmpty()) {
                if (isServer()) {
                    runOnLoopThread([this] { onOutgoingComplete(); }, false);
                } else {
                    onOutgoingComplete();
                }
            } else {
                wait_outgoing_complete_ = true;
            }
        }
    }
    return ret;
}

void H1xStream::onConnect(KMError err)
{// TcpConnection.onConnect
    if (err == KMError::NOERR) {
        err = sendHeaders(buildRequest());
    }
    if(err != KMError::NOERR) {
        onStreamError(err);
    }
}

KMError H1xStream::handleInputData(uint8_t *src, size_t len)
{// TcpConnection.handleInputData
    int bytes_used = incoming_parser_.parse((char*)src, len);
    if (bytes_used != len) {
        
    }
    return KMError::NOERR;
}

void H1xStream::onWrite()
{// TcpConnection.onWrite
    if (wait_outgoing_complete_) {
        wait_outgoing_complete_ = false;
        onOutgoingComplete();
    } else if (write_cb_) {
        write_cb_(KMError::NOERR);
    }
}

void H1xStream::onError(KMError err)
{// TcpConnection.onError
    bool is_complete = incoming_parser_.setEOF();
    if(is_complete) {
        return;
    } else {
        onStreamError(err);
    }
}

void H1xStream::onHttpData(KMBuffer &buf)
{
    onStreamData(buf);
}

void H1xStream::onHttpEvent(HttpEvent ev)
{
    KUMA_INFOXTRACE("onHttpEvent, ev="<<int(ev));
    switch (ev) {
        case HttpEvent::HEADER_COMPLETE:
            onHeaderComplete();
            break;
            
        case HttpEvent::COMPLETE:
            onIncomingComplete();
            break;
            
        case HttpEvent::HTTP_ERROR:
            onError(KMError::FAILED);
            break;
            
        default:
            break;
    }
}

void H1xStream::onHeaderComplete()
{
    if (header_cb_) header_cb_();
}

void H1xStream::onStreamData(KMBuffer &buf)
{
    if (data_cb_) data_cb_(buf);
}

void H1xStream::onOutgoingComplete()
{
    if (outgoing_complete_cb_) outgoing_complete_cb_();
}

void H1xStream::onIncomingComplete()
{
    if (incoming_complete_cb_) incoming_complete_cb_();
}

void H1xStream::onStreamError(KMError err)
{
    if(error_cb_) error_cb_(err);
}

void H1xStream::reset()
{
    TcpConnection::reset();
    outgoing_message_.reset();
    wait_outgoing_complete_ = false;
    incoming_parser_.reset();
}

KMError H1xStream::close()
{
    TcpConnection::close();
    loop_token_.reset();
    return KMError::NOERR;
}
