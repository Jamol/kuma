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
#include "libkev/src/util/kmtrace.h"

#include <iterator>

using namespace kuma;

//////////////////////////////////////////////////////////////////////////
Http1xResponse::Http1xResponse(const EventLoopPtr &loop, std::string ver)
: HttpResponse::Impl(std::move(ver)), stream_(new H1xStream(loop))
{
    stream_->setHeaderCallback([this] () {
        onRequestHeaderComplete();
    });
    stream_->setDataCallback([this] (KMBuffer &buf) {
        onRequestData(buf);
    });
    stream_->setWriteCallback([this] (KMError err) {
        onWrite();
    });
    stream_->setErrorCallback([this] (KMError err) {
        cleanup();
        setState(State::IN_ERROR);
        if(error_cb_) error_cb_(KMError::FAILED);
    });
    stream_->setIncomingCompleteCallback([this] {
        onRequestComplete();
    });
    stream_->setOutgoingCompleteCallback([this] {
        notifyComplete();
    });
    KM_SetObjKey("Http1xResponse");
}

Http1xResponse::~Http1xResponse()
{
    
}

void Http1xResponse::cleanup()
{
    stream_->close();
}

KMError Http1xResponse::setSslFlags(uint32_t ssl_flags)
{
    return stream_->setSslFlags(ssl_flags);
}

KMError Http1xResponse::attachFd(SOCKET_FD fd, const KMBuffer *init_buf)
{
    setState(State::RECVING_REQUEST);
    return stream_->attachFd(fd, init_buf);
}

KMError Http1xResponse::attachSocket(TcpSocket::Impl&& tcp, HttpParser::Impl&& parser, const KMBuffer *init_buf)
{
    setState(State::RECVING_REQUEST);
    return stream_->attachSocket(std::move(tcp), std::move(parser), init_buf);
}

KMError Http1xResponse::addHeader(std::string name, std::string value)
{
    return stream_->addHeader(std::move(name), std::move(value));
}

HttpHeader& Http1xResponse::getRequestHeader()
{
    return stream_->getIncomingHeaders();
}

const HttpHeader& Http1xResponse::getRequestHeader() const
{
    return stream_->getIncomingHeaders();
}

HttpHeader& Http1xResponse::getResponseHeader()
{
    return stream_->getOutgoingHeaders();
}

const HttpHeader& Http1xResponse::getResponseHeader() const
{
    return stream_->getOutgoingHeaders();
}

KMError Http1xResponse::sendResponse(int status_code, const std::string& desc, const std::string& ver)
{
    KM_INFOXTRACE("sendResponse, status_code="<<status_code);
    return stream_->sendResponse(status_code, desc, ver);
}

bool Http1xResponse::canSendBody() const
{
    return stream_->canSendData() && getState() == State::SENDING_RESPONSE;
}

int Http1xResponse::sendBody(const void* data, size_t len)
{
    auto ret = stream_->sendData(data, len);
    if(ret < 0) {
        setState(State::IN_ERROR);
    }
    return ret;
}

int Http1xResponse::sendBody(const KMBuffer &buf)
{
    auto ret = stream_->sendData(buf);
    if(ret < 0) {
        setState(State::IN_ERROR);
    }
    return ret;
}

void Http1xResponse::reset()
{
    HttpResponse::Impl::reset();
    stream_->reset();
}

KMError Http1xResponse::close()
{
    KM_INFOXTRACE("close");
    cleanup();
    setState(State::CLOSED);
    return KMError::NOERR;
}

void Http1xResponse::onWrite()
{
    onSendReady();
}

void Http1xResponse::onError(KMError err)
{
    KM_INFOXTRACE("onError, err="<<int(err));
    cleanup();
    if(getState() < State::COMPLETE) {
        setState(State::IN_ERROR);
        if(error_cb_) error_cb_(err);
    } else {
        setState(State::CLOSED);
    }
}
