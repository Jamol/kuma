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

#include "Http1xRequest.h"
#include "util/kmtrace.h"
#include "util/util.h"
#include "HttpCache.h"

#include <sstream>
#include <iterator>

using namespace kuma;

//////////////////////////////////////////////////////////////////////////
Http1xRequest::Http1xRequest(const EventLoopPtr &loop, std::string ver)
: HttpRequest::Impl(std::move(ver)), TcpConnection(loop), stream_(loop)
{
    loop_token_.eventLoop(loop);
    
    stream_.setHeaderCallback([this] () {
        onResponseHeaderComplete();
    });
    stream_.setDataCallback([this] (KMBuffer &buf) {
        onResponseData(buf);
    });
    stream_.setErrorCallback([this] (KMError err) {
        cleanup();
        setState(State::IN_ERROR);
        if(error_cb_) error_cb_(KMError::FAILED);
        //onError(err);
    });
    stream_.setIncomingCompleteCallback([this] {
        onResponseComplete();
    });
    stream_.setOutgoingCompleteCallback([this] {
        onRequestComplete();
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
    KM_SetObjKey("Http1xRequest");
}

Http1xRequest::~Http1xRequest()
{
    loop_token_.reset();
}

void Http1xRequest::cleanup()
{
    TcpConnection::close();
    loop_token_.reset();
}

KMError Http1xRequest::addHeader(std::string name, std::string value)
{
    return stream_.addHeader(std::move(name), std::move(value));
}

HttpHeader& Http1xRequest::getRequestHeader()
{
    return stream_.getOutgoingHeaders();
}

const HttpHeader& Http1xRequest::getRequestHeader() const
{
    return stream_.getOutgoingHeaders();
}

HttpHeader& Http1xRequest::getResponseHeader()
{
    return stream_.getIncomingHeaders();
}

const HttpHeader& Http1xRequest::getResponseHeader() const
{
    return stream_.getIncomingHeaders();
}

KMError Http1xRequest::sendRequest()
{
    if (processHttpCache()) {
        return KMError::NOERR;
    }
    if (getState() == State::IDLE) {
        setState(State::CONNECTING);
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
    } else { // connection reuse
        sendRequestHeader();
        return KMError::NOERR;
    }
}

bool Http1xRequest::canSendBody() const
{
    return sendBufferEmpty() && getState() == State::SENDING_BODY;
}

int Http1xRequest::sendBody(const void* data, size_t len)
{
    auto ret = stream_.sendData(data, len);
    if(ret < 0) {
        setState(State::IN_ERROR);
    }
    return ret;
}

int Http1xRequest::sendBody(const KMBuffer &buf)
{
    auto ret = stream_.sendData(buf);
    if(ret < 0) {
        setState(State::IN_ERROR);
    }
    return ret;
}

void Http1xRequest::reset()
{
    HttpRequest::Impl::reset();
    stream_.reset();
    rsp_cache_status_ = 0;
    rsp_cache_header_.reset();
    rsp_cache_body_.reset();
    if (getState() == State::COMPLETE) {
        setState(State::WAIT_FOR_REUSE);
    }
}

KMError Http1xRequest::close()
{
    KUMA_INFOXTRACE("close");
    cleanup();
    setState(State::CLOSED);
    return KMError::NOERR;
}

KMError Http1xRequest::sendRequestHeader()
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
    setState(State::SENDING_HEADER);
    auto ret = stream_.sendRequest(method_, url, version_);
    if(ret != KMError::NOERR) {
        cleanup();
        setState(State::IN_ERROR);
        if(error_cb_) error_cb_(ret);
    } else if (sendBufferEmpty()) {
        if(!getRequestHeader().hasBody()) {
            onRequestComplete();
        } else {
            setState(State::SENDING_BODY);
            onSendReady();
        }
    }
    return ret;
}

void Http1xRequest::onConnect(KMError err)
{
    if(err != KMError::NOERR) {
        if(error_cb_) error_cb_(err);
        return ;
    }
    sendRequestHeader();
}

KMError Http1xRequest::handleInputData(uint8_t *src, size_t len)
{
    DESTROY_DETECTOR_SETUP();
    stream_.handleInputData(src, len);
    DESTROY_DETECTOR_CHECK(KMError::DESTROYED);
    if(getState() == State::IN_ERROR || getState() == State::CLOSED) {
        return KMError::FAILED;
    }

    return KMError::NOERR;
}

void Http1xRequest::onWrite()
{
    if (getState() == State::SENDING_HEADER) {
        if(!getRequestHeader().hasBody()) {
            onRequestComplete();
            return;
        } else {
            setState(State::SENDING_BODY);
            onSendReady();
        }
    } else if (getState() == State::SENDING_BODY) {
        onSendReady();
    }
}

void Http1xRequest::onError(KMError err)
{
    KUMA_INFOXTRACE("onError, err="<<int(err));
    if (getState() == State::RECVING_RESPONSE) {
        DESTROY_DETECTOR_SETUP();
        bool is_complete = stream_.setEOF();
        DESTROY_DETECTOR_CHECK_VOID();
        if(is_complete) {
            cleanup();
            return;
        }
    }
    cleanup();
    if(getState() < State::COMPLETE) {
        setState(State::IN_ERROR);
        if(error_cb_) error_cb_(KMError::SOCK_ERROR);
    } else {
        setState(State::CLOSED);
    }
}

void Http1xRequest::onRequestComplete()
{
    setState(State::RECVING_RESPONSE);
}

bool Http1xRequest::processHttpCache()
{
    if (!HttpCache::isCacheable(method_, getRequestHeader().getHeaders())) {
        return false;
    }
    std::string cache_key = getCacheKey();
    
    int status_code = 0;
    HeaderVector rsp_headers;
    KMBuffer rsp_body;
    if (HttpCache::instance().getCache(cache_key, status_code, rsp_headers, rsp_body)) {
        // cache hit
        setState(State::RECVING_RESPONSE);
        rsp_cache_header_.setHeaders(std::move(rsp_headers));
        rsp_cache_status_ = status_code;
        //rsp_parser_.setHeaders(std::move(rsp_headers));
        //rsp_parser_.setStatusCode(status_code);
        rsp_cache_body_.reset(rsp_body.clone());
        eventLoop()->post([this] { onCacheComplete(); }, &loop_token_);
        return true;
    }
    return false;
}

void Http1xRequest::onCacheComplete()
{
    if (getState() != State::RECVING_RESPONSE) {
        return;
    }
    DESTROY_DETECTOR_SETUP();
    onResponseHeaderComplete();
    DESTROY_DETECTOR_CHECK_VOID();
    if (rsp_cache_body_ && !rsp_cache_body_->empty() && data_cb_) {
        DESTROY_DETECTOR_SETUP();
        onResponseData(*rsp_cache_body_);
        DESTROY_DETECTOR_CHECK_VOID();
        rsp_cache_body_.reset();
    }
    onResponseComplete();
}
