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
        if (req_parser_.headerComplete()) {
            DESTROY_DETECTOR_SETUP();
            onRequestHeaderComplete();
            DESTROY_DETECTOR_CHECK(KMError::DESTROYED);
        }
        req_parser_.resume();
    }
    return ret;
}

KMError Http1xResponse::addHeader(std::string name, std::string value)
{
    return rsp_message_.addHeader(std::move(name), std::move(value));
}

void Http1xResponse::checkResponseHeaders()
{
    if (!rsp_message_.hasHeader(strContentType)) {
        addHeader(strContentType, "application/octet-stream");
    }
    
    if (!encoding_type_.empty()) {
        if (is_content_encoding_) {
            if (!rsp_message_.hasHeader(strContentEncoding)) {
                addHeader(strContentEncoding, encoding_type_);
                KUMA_INFOXTRACE("checkResponseHeaders, add Content-Encoding="<<encoding_type_);
            }
        } else {
            addHeader(strTransferEncoding, encoding_type_ + ", chunked");
            KUMA_INFOXTRACE("checkResponseHeaders, add Transfer-Encoding="<<encoding_type_);
        }
    }
}

void Http1xResponse::checkRequestHeaders()
{
    is_content_encoding_ = true;
    auto encodings = req_parser_.getHeaderValue(strAcceptEncoding);
    if (encodings.empty()) {
        encodings = req_parser_.getHeaderValue("TE");
        is_content_encoding_ = !encodings.empty();
    }
    for_each_token(encodings, ',', [this] (const std::string &str) {
        if (is_equal(str, "gzip")) {
            encoding_type_ = "gzip";
            return false;
        } else if (is_equal(str, "deflate")) {
            encoding_type_ = "deflate";
            return false;
        }
        return true;
    });
}

const HttpHeader& Http1xResponse::getRequestHeader() const
{
    return req_parser_;
}

HttpHeader& Http1xResponse::getResponseHeader()
{
    return rsp_message_;
}

void Http1xResponse::buildResponse(int status_code, const std::string& desc, const std::string& ver)
{
    auto rsp = rsp_message_.buildHeader(status_code, desc, ver, req_parser_.getMethod());
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
            eventLoop()->post([this] { onSendReady(); }, &loop_token_);
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

int Http1xResponse::sendBody(const KMBuffer &buf)
{
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
    encoding_type_.clear();
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

void Http1xResponse::onHttpData(KMBuffer &buf)
{
    onRequestData(buf);
}

void Http1xResponse::onHttpEvent(HttpEvent ev)
{
    KUMA_INFOXTRACE("onHttpEvent, ev="<<int(ev));
    switch (ev) {
        case HttpEvent::HEADER_COMPLETE:
            onRequestHeaderComplete();
            break;
            
        case HttpEvent::COMPLETE:
            onRequestComplete();
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
