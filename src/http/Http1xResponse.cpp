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

#include "Http1xResponse.h"
#include "EventLoopImpl.h"
#include "util/kmtrace.h"

#include <iterator>

using namespace kuma;

//////////////////////////////////////////////////////////////////////////
Http1xResponse::Http1xResponse(const EventLoopPtr &loop, std::string ver)
: HttpResponse::Impl(std::move(ver)), TcpConnection(loop)
{
    loop_token_.eventLoop(loop);
    rsp_message_.setSender([this] (const void* data, size_t len) -> int {
        return TcpConnection::send(data, len);
    });
    rsp_message_.setVSender([this] (const iovec* iovs, int count) -> int {
        return TcpConnection::send(iovs, count);
    });
    rsp_message_.setBSender([this] (const KMBuffer &buf) -> int {
        return TcpConnection::send(buf);
    });
    KM_SetObjKey("Http1xResponse");
}

Http1xResponse::~Http1xResponse()
{
    
}

void Http1xResponse::cleanup()
{
    TcpConnection::close();
    loop_token_.reset();
}

KMError Http1xResponse::setSslFlags(uint32_t ssl_flags)
{
    return TcpConnection::setSslFlags(ssl_flags);
}

KMError Http1xResponse::attachFd(SOCKET_FD fd, const KMBuffer *init_buf)
{
    setState(State::RECVING_REQUEST);
    req_parser_.reset();
    req_parser_.setDataCallback([this] (KMBuffer &buf) { onHttpData(buf); });
    req_parser_.setEventCallback([this] (HttpEvent ev) { onHttpEvent(ev); });
    return TcpConnection::attachFd(fd, init_buf);
}

KMError Http1xResponse::attachSocket(TcpSocket::Impl&& tcp, HttpParser::Impl&& parser, const KMBuffer *init_buf)
{
    setState(State::RECVING_REQUEST);
    req_parser_.reset();
    req_parser_ = std::move(parser);
    req_parser_.setDataCallback([this] (KMBuffer &buf) { onHttpData(buf); });
    req_parser_.setEventCallback([this] (HttpEvent ev) { onHttpEvent(ev); });

    auto ret = TcpConnection::attachSocket(std::move(tcp), init_buf);
    if(ret == KMError::NOERR && req_parser_.paused()) {
        req_parser_.resume();
    }
    return ret;
}

KMError Http1xResponse::addHeader(std::string name, std::string value)
{
    rsp_message_.addHeader(std::move(name), std::move(value));
    return KMError::NOERR;
}

void Http1xResponse::checkHeaders()
{
    if(!rsp_message_.hasHeader(strContentType)) {
        addHeader(strContentType, "application/octet-stream");
    }
}

void Http1xResponse::buildResponse(int status_code, const std::string& desc, const std::string& ver)
{
    auto rsp = rsp_message_.buildHeader(status_code, desc, ver);
    KMBuffer buf(rsp.c_str(), rsp.size(), rsp.size());
    appendSendBuffer(buf);
}

KMError Http1xResponse::sendResponse(int status_code, const std::string& desc, const std::string& ver)
{
    KUMA_INFOXTRACE("sendResponse, status_code="<<status_code);
    if (getState() != State::WAIT_FOR_RESPONSE) {
        return KMError::INVALID_STATE;
    }
    buildResponse(status_code, desc, ver);
    setState(State::SENDING_HEADER);
    auto ret = sendBufferedData();
    if(ret != KMError::NOERR) {
        cleanup();
        setState(State::IN_ERROR);
        return KMError::SOCK_ERROR;
    } else if (sendBufferEmpty()) {
        if(!rsp_message_.hasBody()) {
            setState(State::COMPLETE);
            eventLoop()->post([this] { notifyComplete(); }, &loop_token_);
        } else {
            setState(State::SENDING_BODY);
            eventLoop()->post([this] { if (write_cb_) write_cb_(KMError::NOERR); }, &loop_token_);
        }
    }
    return KMError::NOERR;
}

int Http1xResponse::sendData(const void* data, size_t len)
{
    if(!sendBufferEmpty() || getState() != State::SENDING_BODY) {
        return 0;
    }
    int ret = rsp_message_.sendData(data, len);
    if(ret < 0) {
        setState(State::IN_ERROR);
    } else if(ret >= 0) {
        if (rsp_message_.isCompleted() && sendBufferEmpty()) {
            setState(State::COMPLETE);
            eventLoop()->post([this] { notifyComplete(); }, &loop_token_);
        }
    }
    return ret;
}

int Http1xResponse::sendData(const KMBuffer &buf)
{
    if(!sendBufferEmpty() || getState() != State::SENDING_BODY) {
        return 0;
    }
    int ret = rsp_message_.sendData(buf);
    if(ret < 0) {
        setState(State::IN_ERROR);
    } else if(ret >= 0) {
        if (rsp_message_.isCompleted() && sendBufferEmpty()) {
            setState(State::COMPLETE);
            eventLoop()->post([this] { notifyComplete(); }, &loop_token_);
        }
    }
    return ret;
}

void Http1xResponse::reset()
{
    // reset TcpConnection
    TcpConnection::reset();
    
    HttpResponse::Impl::reset();
    req_parser_.reset();
    rsp_message_.reset();
    setState(State::RECVING_REQUEST);
}

KMError Http1xResponse::close()
{
    KUMA_INFOXTRACE("close");
    cleanup();
    setState(State::CLOSED);
    return KMError::NOERR;
}

KMError Http1xResponse::handleInputData(uint8_t *src, size_t len)
{
    DESTROY_DETECTOR_SETUP();
    int bytes_used = req_parser_.parse((char*)src, len);
    DESTROY_DETECTOR_CHECK(KMError::DESTROYED);
    if(getState() == State::IN_ERROR || getState() == State::CLOSED) {
        return KMError::FAILED;
    }
    if(bytes_used != len) {
        KUMA_WARNXTRACE("handleInputData, bytes_used="<<bytes_used<<", bytes_read="<<len);
    }
    return KMError::NOERR;
}

void Http1xResponse::onWrite()
{
    if (getState() == State::SENDING_HEADER) {
        if(!rsp_message_.hasBody()) {
            setState(State::COMPLETE);
            notifyComplete();
            return;
        } else {
            setState(State::SENDING_BODY);
        }
    } else if (getState() == State::SENDING_BODY) {
        if(rsp_message_.isCompleted()) {
            setState(State::COMPLETE);
            notifyComplete();
            return ;
        }
    }
    if(write_cb_) write_cb_(KMError::NOERR);
}

void Http1xResponse::onError(KMError err)
{
    KUMA_INFOXTRACE("onError, err="<<int(err));
    cleanup();
    if(getState() < State::COMPLETE) {
        setState(State::IN_ERROR);
        if(error_cb_) error_cb_(KMError::SOCK_ERROR);
    } else {
        setState(State::CLOSED);
    }
}

void Http1xResponse::onHttpData(KMBuffer &buf)
{
    if(data_cb_) data_cb_(buf);
}

void Http1xResponse::onHttpEvent(HttpEvent ev)
{
    KUMA_INFOXTRACE("onHttpEvent, ev="<<int(ev));
    switch (ev) {
        case HttpEvent::HEADER_COMPLETE:
            if(header_cb_) header_cb_();
            break;
            
        case HttpEvent::COMPLETE:
            setState(State::WAIT_FOR_RESPONSE);
            if(request_cb_) request_cb_();
            break;
            
        case HttpEvent::HTTP_ERROR:
            cleanup();
            setState(State::IN_ERROR);
            if(error_cb_) error_cb_(KMError::FAILED);
            break;
            
        default:
            break;
    }
}
