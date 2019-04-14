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
: HttpResponse::Impl(std::move(ver)), TcpConnection(loop), stream_(loop)
{
    loop_token_.eventLoop(loop);
    
    stream_.setHeaderCallback([this] () {
        onRequestHeaderComplete();
    });
    stream_.setDataCallback([this] (KMBuffer &buf) {
        onRequestData(buf);
    });
    stream_.setErrorCallback([this] (KMError err) {
        cleanup();
        setState(State::IN_ERROR);
        if(error_cb_) error_cb_(KMError::FAILED);
    });
    stream_.setIncomingCompleteCallback([this] {
        onRequestComplete();
    });
    stream_.setOutgoingCompleteCallback([this] {
        // triggered by sendBody, wait until all data is sent out
        if (sendBufferEmpty()) {
            setState(State::COMPLETE);
            eventLoop()->post([this] { notifyComplete(); }, &loop_token_);
        }
    });
    
    stream_.setSender([this] (const void* data, size_t len) -> int {
        return TcpConnection::send(data, len);
    });
    stream_.setVSender([this] (const iovec* iovs, int count) -> int {
        return TcpConnection::send(iovs, count);
    });
    stream_.setBSender([this] (const KMBuffer &buf) -> int {
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
    return TcpConnection::attachFd(fd, init_buf);
}

KMError Http1xResponse::attachSocket(TcpSocket::Impl&& tcp, HttpParser::Impl&& parser, const KMBuffer *init_buf)
{
    setState(State::RECVING_REQUEST);
    bool is_parser_paused = parser.paused();
    bool is_parser_header_complete = parser.headerComplete();
    bool is_parser_complete = parser.complete();
    stream_.setHttpParser(std::move(parser));

    auto ret = TcpConnection::attachSocket(std::move(tcp), init_buf);
    if(ret == KMError::NOERR && is_parser_paused) {
        if (is_parser_header_complete) {
            DESTROY_DETECTOR_SETUP();
            onRequestHeaderComplete();
            DESTROY_DETECTOR_CHECK(KMError::DESTROYED);
        }
        if (is_parser_complete) {
            onRequestComplete();
        }
    }
    return ret;
}

KMError Http1xResponse::addHeader(std::string name, std::string value)
{
    return stream_.addHeader(std::move(name), std::move(value));
}

HttpHeader& Http1xResponse::getRequestHeader()
{
    return stream_.getIncomingHeaders();
}

const HttpHeader& Http1xResponse::getRequestHeader() const
{
    return stream_.getIncomingHeaders();
}

HttpHeader& Http1xResponse::getResponseHeader()
{
    return stream_.getOutgoingHeaders();
}

const HttpHeader& Http1xResponse::getResponseHeader() const
{
    return stream_.getOutgoingHeaders();
}

KMError Http1xResponse::sendResponse(int status_code, const std::string& desc, const std::string& ver)
{
    KUMA_INFOXTRACE("sendResponse, status_code="<<status_code);
    if (getState() != State::WAIT_FOR_RESPONSE) {
        return KMError::INVALID_STATE;
    }
    setState(State::SENDING_HEADER);
    auto ret = stream_.sendResponse(status_code, desc, ver);
    if(ret != KMError::NOERR) {
        cleanup();
        setState(State::IN_ERROR);
        return ret;
    } else if (sendBufferEmpty()) {
        if(!getResponseHeader().hasBody()) {
            setState(State::COMPLETE);
            eventLoop()->post([this] { notifyComplete(); }, &loop_token_);
        } else {
            setState(State::SENDING_BODY);
            //eventLoop()->post([this] { onSendReady(); }, &loop_token_);
            onSendReady();
        }
    }
    return KMError::NOERR;
}

bool Http1xResponse::canSendBody() const
{
    return sendBufferEmpty() && getState() == State::SENDING_BODY;
}

int Http1xResponse::sendBody(const void* data, size_t len)
{
    auto ret = stream_.sendData(data, len);
    if(ret < 0) {
        setState(State::IN_ERROR);
    }
    return ret;
}

int Http1xResponse::sendBody(const KMBuffer &buf)
{
    auto ret = stream_.sendData(buf);
    if(ret < 0) {
        setState(State::IN_ERROR);
    }
    return ret;
}

void Http1xResponse::reset()
{
    // reset TcpConnection
    TcpConnection::reset();
    
    HttpResponse::Impl::reset();
    stream_.reset();
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
    stream_.handleInputData(src, len);
    DESTROY_DETECTOR_CHECK(KMError::DESTROYED);
    if(getState() == State::IN_ERROR || getState() == State::CLOSED) {
        return KMError::FAILED;
    }
    
    return KMError::NOERR;
}

void Http1xResponse::onWrite()
{
    if (getState() == State::SENDING_HEADER) {
        if(!getResponseHeader().hasBody()) {
            setState(State::COMPLETE);
            notifyComplete();
            return;
        } else {
            setState(State::SENDING_BODY);
        }
    } else if (getState() == State::SENDING_BODY) {
        if(stream_.isOutgoingComplete()) {
            setState(State::COMPLETE);
            notifyComplete();
            return ;
        }
    }
    onSendReady();
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
