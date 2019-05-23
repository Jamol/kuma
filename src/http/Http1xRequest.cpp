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
: HttpRequest::Impl(std::move(ver)), stream_(new H1xStream(loop))
{
    stream_->setHeaderCallback([this] () {
        onResponseHeaderComplete();
    });
    stream_->setDataCallback([this] (KMBuffer &buf) {
        onResponseData(buf);
    });
    stream_->setWriteCallback([this] (KMError err) {
        onWrite();
    });
    stream_->setErrorCallback([this] (KMError err) {
        cleanup();
        setState(State::IN_ERROR);
        if(error_cb_) error_cb_(err);
        //onError(err);
    });
    stream_->setIncomingCompleteCallback([this] {
        onResponseComplete();
    });
    stream_->setOutgoingCompleteCallback([this] {
        onRequestComplete();
    });
    
    KM_SetObjKey("Http1xRequest");
}

Http1xRequest::~Http1xRequest()
{
    
}

void Http1xRequest::cleanup()
{
    stream_->close();
}

KMError Http1xRequest::setProxyInfo(const ProxyInfo &proxy_info)
{
    return stream_->setProxyInfo(proxy_info);
}

KMError Http1xRequest::addHeader(std::string name, std::string value)
{
    return stream_->addHeader(std::move(name), std::move(value));
}

HttpHeader& Http1xRequest::getRequestHeader()
{
    return stream_->getOutgoingHeaders();
}

const HttpHeader& Http1xRequest::getRequestHeader() const
{
    return stream_->getOutgoingHeaders();
}

HttpHeader& Http1xRequest::getResponseHeader()
{
    return stream_->getIncomingHeaders();
}

const HttpHeader& Http1xRequest::getResponseHeader() const
{
    return stream_->getIncomingHeaders();
}

KMError Http1xRequest::sendRequest()
{
    if (processHttpCache()) {
        return KMError::NOERR;
    }
    return stream_->sendRequest(method_, url_, version_);
}

int Http1xRequest::getStatusCode() const
{
    if (rsp_cache_status_ != 0) {
        return rsp_cache_status_;
    } else {
        return stream_->getStatusCode();
    }
}

bool Http1xRequest::canSendBody() const
{
    return stream_->canSendData() && getState() == State::SENDING_REQUEST;
}

int Http1xRequest::sendBody(const void* data, size_t len)
{
    auto ret = stream_->sendData(data, len);
    if(ret < 0) {
        setState(State::IN_ERROR);
    }
    return ret;
}

int Http1xRequest::sendBody(const KMBuffer &buf)
{
    auto ret = stream_->sendData(buf);
    if(ret < 0) {
        setState(State::IN_ERROR);
    }
    return ret;
}

void Http1xRequest::reset()
{
    HttpRequest::Impl::reset();
    stream_->reset();
    rsp_cache_status_ = 0;
    rsp_cache_body_.reset();
}

KMError Http1xRequest::close()
{
    KUMA_INFOXTRACE("close");
    cleanup();
    setState(State::CLOSED);
    return KMError::NOERR;
}

void Http1xRequest::onWrite()
{
    if (getState() == State::SENDING_REQUEST) {
        onSendReady();
    }
}

void Http1xRequest::onError(KMError err)
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
        auto &rsp_header = getResponseHeader();
        rsp_header.setHeaders(std::move(rsp_headers));
        rsp_cache_status_ = status_code;
        rsp_cache_body_.reset(rsp_body.clone());
        stream_->runOnLoopThread([this] { onCacheComplete(); }, false);
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
