/* Copyright (c) 2016, Fengping Bao <jamol@live.com>
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

#include "Http2Request.h"
#include "http/HttpCache.h"
#include "H2StreamProxy.h"
#include "libkev/src/utils/kmtrace.h"
#include "libkev/src/utils/utils.h"

#include <string>

using namespace kuma;

Http2Request::Http2Request(const EventLoopPtr &loop, std::string ver)
: HttpRequest::Impl(std::move(ver)), stream_(new H2StreamProxy(loop))
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
        onComplete();
    });
    stream_->setOutgoingCompleteCallback([this] {
        onRequestComplete();
    });
    KM_SetObjKey("Http2Request");
}

Http2Request::~Http2Request()
{
    
}

KMError Http2Request::setSslFlags(uint32_t ssl_flags)
{
    ssl_flags_ = ssl_flags;
    return KMError::NOERR;
}

KMError Http2Request::setProxyInfo(const ProxyInfo &proxy_info)
{
    return stream_->setProxyInfo(proxy_info);
}

KMError Http2Request::addHeader(std::string name, std::string value)
{
    return stream_->addHeader(std::move(name), std::move(value));
}

KMError Http2Request::sendRequest()
{
    if (processHttpCache()) {
        // cache hit
        return KMError::NOERR;
    }
    
    setState(State::SENDING_REQUEST);
    return stream_->sendRequest(method_, url_, ssl_flags_);
}

int Http2Request::getStatusCode() const
{
    if (rsp_cache_status_ != 0) {
        return rsp_cache_status_;
    } else {
        return stream_->getStatusCode();
    }
}

void Http2Request::checkResponseHeaders()
{
    HttpRequest::Impl::checkResponseHeaders();
    
    auto const &rsp_header = getResponseHeader();
    if (rsp_header.hasContentLength()) {
        KM_INFOXTRACE("checkResponseHeaders, Content-Length=" << rsp_header.getContentLength());
    }
}

HttpHeader& Http2Request::getRequestHeader()
{
    return stream_->getOutgoingHeaders();
}

const HttpHeader& Http2Request::getRequestHeader() const
{
    return stream_->getOutgoingHeaders();
}

HttpHeader& Http2Request::getResponseHeader()
{
    return stream_->getIncomingHeaders();
}

const HttpHeader& Http2Request::getResponseHeader() const
{
    return stream_->getIncomingHeaders();
}

bool Http2Request::canSendBody() const
{
    return stream_->canSendData() && getState() == State::SENDING_REQUEST;
}

int Http2Request::sendBody(const void* data, size_t len)
{
    return stream_->sendData(data, len);
}

int Http2Request::sendBody(const KMBuffer &buf)
{
    return stream_->sendData(buf);
}

KMError Http2Request::close()
{
    if (getState() != State::CLOSED) {
        KM_INFOXTRACE("close");
        stream_->close();
        reset();
        setState(State::CLOSED);
    }
    return KMError::NOERR;
}

void Http2Request::onData(KMBuffer &buf)
{
    onResponseData(buf);
}

void Http2Request::onHeader()
{
    onResponseHeaderComplete();
}

void Http2Request::onRequestComplete()
{
    setState(State::RECVING_RESPONSE);
}

void Http2Request::onComplete()
{
    onResponseComplete();
}

void Http2Request::onWrite()
{
    onSendReady();
}

void Http2Request::onError(KMError err)
{
    if(error_cb_) error_cb_(err);
}

void Http2Request::reset()
{
    HttpRequest::Impl::reset();
    
    stream_->close();
    ssl_flags_ = 0;
    rsp_cache_status_ = 0;
    rsp_cache_body_.reset();
}

bool Http2Request::processHttpCache()
{
    auto const &req_header = getRequestHeader();
    if (!HttpCache::isCacheable(method_, req_header.getHeaders())) {
        return false;
    }
    std::string cache_key = getCacheKey();
    
    int status_code = 0;
    HeaderVector rsp_headers;
    KMBuffer rsp_body;
    if (HttpCache::instance().getCache(cache_key, status_code, rsp_headers, rsp_body)) {
        // cache hit
        setState(State::RECVING_RESPONSE);
        auto &rsp_header = getResponseHeader();
        rsp_header.setHeaders(std::move(rsp_headers));
        rsp_cache_status_ = status_code;
        rsp_cache_body_.reset(rsp_body.clone());
        stream_->runOnLoopThread([this] { onCacheComplete(); });
        return true;
    }
    return false;
}

void Http2Request::onCacheComplete()
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
