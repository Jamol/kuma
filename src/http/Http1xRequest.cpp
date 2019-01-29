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
: HttpRequest::Impl(std::move(ver)), TcpConnection(loop), rsp_parser_()
{
    loop_token_.eventLoop(loop);
    req_message_.setSender([this] (const void* data, size_t len) -> int {
        return TcpConnection::send(data, len);
    });
    req_message_.setVSender([this] (const iovec* iovs, int count) -> int {
        return TcpConnection::send(iovs, count);
    });
    req_message_.setBSender([this] (const KMBuffer &buf) -> int {
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
    req_message_.addHeader(std::move(name), std::move(value));
    return KMError::NOERR;
}

void Http1xRequest::checkHeaders()
{
    if(!req_message_.hasHeader("Accept")) {
        addHeader("Accept", "*/*");
    }
    if(!req_message_.hasHeader(strContentType)) {
        addHeader(strContentType, "application/octet-stream");
    }
    if(!req_message_.hasHeader("User-Agent")) {
        addHeader("User-Agent", UserAgent);
    }
    addHeader("Host", uri_.getHost());
    if(!req_message_.hasHeader("Cache-Control")) {
        addHeader("Cache-Control", "no-cache");
    }
    if(!req_message_.hasHeader("Pragma")) {
        addHeader("Pragma", "no-cache");
    }
}

void Http1xRequest::buildRequest()
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
    auto req = req_message_.buildHeader(method_, url, version_);
    KMBuffer buf(req.c_str(), req.size(), req.size());
    appendSendBuffer(buf);
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

bool Http1xRequest::processHttpCache()
{
    if (!HttpCache::isCacheable(method_, req_message_.getHeaders())) {
        return false;
    }
    std::string cache_key = getCacheKey();
    
    int status_code = 0;
    HeaderVector rsp_headers;
    KMBuffer rsp_body;
    if (HttpCache::instance().getCache(cache_key, status_code, rsp_headers, rsp_body)) {
        // cache hit
        setState(State::RECVING_RESPONSE);
        rsp_parser_.setHeaders(std::move(rsp_headers));
        rsp_parser_.setStatusCode(status_code);
        rsp_cache_body_.reset(rsp_body.clone());
        auto loop = TcpConnection::eventLoop();
        loop->post([this] { onCacheComplete(); }, &loop_token_);
        return true;
    }
    return false;
}

int Http1xRequest::sendData(const void* data, size_t len)
{
    if(!sendBufferEmpty() || getState() != State::SENDING_BODY) {
        return 0;
    }
    auto ret = req_message_.sendData(data, len);
    if (ret >= 0) {
        if (req_message_.isCompleted() && sendBufferEmpty()) {
            setState(State::RECVING_RESPONSE);
        }
    } else if(ret < 0) {
        setState(State::IN_ERROR);
    }
    return ret;
}

int Http1xRequest::sendData(const KMBuffer &buf)
{
    if(!sendBufferEmpty() || getState() != State::SENDING_BODY) {
        return 0;
    }
    auto ret = req_message_.sendData(buf);
    if (ret >= 0) {
        if (req_message_.isCompleted() && sendBufferEmpty()) {
            setState(State::RECVING_RESPONSE);
        }
    } else if(ret < 0) {
        setState(State::IN_ERROR);
    }
    return ret;
}

void Http1xRequest::reset()
{
    HttpRequest::Impl::reset();
    req_message_.reset();
    rsp_parser_.reset();
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

void Http1xRequest::sendRequestHeader()
{
    rsp_parser_.setDataCallback([this] (KMBuffer &buf) { onHttpData(buf); });
    rsp_parser_.setEventCallback([this] (HttpEvent ev) { onHttpEvent(ev); });
    buildRequest();
    setState(State::SENDING_HEADER);
    auto ret = sendBufferedData();
    if(ret != KMError::NOERR) {
        cleanup();
        setState(State::IN_ERROR);
        if(error_cb_) error_cb_(KMError::SOCK_ERROR);
        return;
    } else if (sendBufferEmpty()) {
        if(!req_message_.hasBody()) {
            setState(State::RECVING_RESPONSE);
        } else {
            setState(State::SENDING_BODY);
            if (write_cb_) {
                write_cb_(KMError::NOERR);
            }
        }
    }
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
    int bytes_used = rsp_parser_.parse((char*)src, len);
    DESTROY_DETECTOR_CHECK(KMError::DESTROYED);
    if(getState() == State::IN_ERROR || getState() == State::CLOSED) {
        return KMError::FAILED;
    }
    if(bytes_used != len) {
        KUMA_WARNXTRACE("handleInputData, bytes_used="<<bytes_used<<", bytes_read="<<len);
    }
    return KMError::NOERR;
}

void Http1xRequest::onWrite()
{
    if (getState() == State::SENDING_HEADER) {
        if(!req_message_.hasBody()) {
            setState(State::RECVING_RESPONSE);
            return;
        } else {
            setState(State::SENDING_BODY);
        }
    } else if (getState() == State::SENDING_BODY) {
        if (req_message_.isCompleted()) {
            setState(State::RECVING_RESPONSE);
            return;
        }
    }
    
    if(write_cb_) write_cb_(KMError::NOERR);
}

void Http1xRequest::onError(KMError err)
{
    KUMA_INFOXTRACE("onError, err="<<int(err));
    if (getState() == State::RECVING_RESPONSE) {
        DESTROY_DETECTOR_SETUP();
        bool completed = rsp_parser_.setEOF();
        DESTROY_DETECTOR_CHECK_VOID();
        if(completed) {
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

void Http1xRequest::onHttpData(KMBuffer &buf)
{
    if(data_cb_) data_cb_(buf);
}

void Http1xRequest::onHttpEvent(HttpEvent ev)
{
    KUMA_INFOXTRACE("onHttpEvent, ev="<<int(ev));
    switch (ev) {
        case HttpEvent::HEADER_COMPLETE:
            if(header_cb_) header_cb_();
            break;
            
        case HttpEvent::COMPLETE:
            onComplete();
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

void Http1xRequest::onComplete()
{
    setState(State::COMPLETE);
    if(response_cb_) response_cb_();
}

void Http1xRequest::onCacheComplete()
{
    if (getState() != State::RECVING_RESPONSE) {
        return;
    }
    DESTROY_DETECTOR_SETUP();
    if (header_cb_) header_cb_();
    DESTROY_DETECTOR_CHECK_VOID();
    if (rsp_cache_body_ && !rsp_cache_body_->empty() && data_cb_) {
        DESTROY_DETECTOR_SETUP();
        data_cb_(*rsp_cache_body_);
        DESTROY_DETECTOR_CHECK_VOID();
        rsp_cache_body_.reset();
    }
    onComplete();
}
