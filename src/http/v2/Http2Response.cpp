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

#include "Http2Response.h"
#include "H2StreamProxy.h"

#include <string>

using namespace kuma;

Http2Response::Http2Response(const EventLoopPtr &loop, std::string ver)
: HttpResponse::Impl(std::move(ver)), stream_(new H2StreamProxy(loop))
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
        onRequestComplete();
    });
    stream_->setOutgoingCompleteCallback([this] {
        notifyComplete();
    });
    KM_SetObjKey("Http2Response");
}

KMError Http2Response::attachStream(uint32_t stream_id, H2Connection::Impl* conn)
{
    setState(State::RECVING_REQUEST);
    return stream_->attachStream(stream_id, conn);
}

KMError Http2Response::addHeader(std::string name, std::string value)
{
    return stream_->addHeader(std::move(name), std::move(value));
}

KMError Http2Response::sendResponse(int status_code, const std::string& desc, const std::string& ver)
{
    return stream_->sendResponse(status_code);
}

bool Http2Response::canSendBody() const
{
    return stream_->canSendData() && getState() == State::SENDING_RESPONSE;
}

int Http2Response::sendBody(const void* data, size_t len)
{
    return stream_->sendData(data, len);
}

int Http2Response::sendBody(const KMBuffer &buf)
{
    return stream_->sendData(buf);
}

KMError Http2Response::close()
{
    KUMA_INFOXTRACE("close");
    stream_->close();
    setState(State::CLOSED);
    return KMError::NOERR;
}

void Http2Response::checkResponseHeaders()
{
    HttpResponse::Impl::checkResponseHeaders();
    
    auto &rsp_header = getResponseHeader();
    if (rsp_header.hasContentLength()) {
        KUMA_INFOXTRACE("checkResponseHeaders, Content-Length=" << rsp_header.getContentLength());
    }
}

void Http2Response::checkRequestHeaders()
{
    HttpResponse::Impl::checkRequestHeaders();
    
    auto const & req_header = getRequestHeader();
    if (req_header.hasContentLength()) {
        KUMA_INFOXTRACE("checkRequestHeaders, Content-Length=" << req_header.getContentLength());
    }
}

const std::string& Http2Response::getMethod() const
{
    return stream_->getMethod();
}

const std::string& Http2Response::getPath() const
{
    return stream_->getPath();
}

HttpHeader& Http2Response::getRequestHeader()
{
    return stream_->getIncomingHeaders();
}

const HttpHeader& Http2Response::getRequestHeader() const
{
    return stream_->getIncomingHeaders();
}

HttpHeader& Http2Response::getResponseHeader()
{
    return stream_->getOutgoingHeaders();
}

const HttpHeader& Http2Response::getResponseHeader() const
{
    return stream_->getOutgoingHeaders();
}

const std::string& Http2Response::getParamValue(const std::string &name) const {
    return EmptyString;
}

const std::string& Http2Response::getHeaderValue(const std::string &name) const {
    return getRequestHeader().getHeader(name);
}

void Http2Response::forEachHeader(const EnumerateCallback &cb) const {
    for (auto &kv : getRequestHeader().getHeaders()) {
        if (!cb(kv.first, kv.second)) {
            break;
        }
    }
}

void Http2Response::onHeader()
{
    onRequestHeaderComplete();
}

void Http2Response::onData(KMBuffer &buf)
{
    onRequestData(buf);
}

void Http2Response::onWrite()
{
    onSendReady();
}

void Http2Response::onError(KMError err)
{
    if(error_cb_) error_cb_(err);
}
