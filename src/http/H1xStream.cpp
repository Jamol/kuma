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
{
    incoming_parser_.setDataCallback([this] (KMBuffer &buf) { onHttpData(buf); });
    incoming_parser_.setEventCallback([this] (HttpEvent ev) { onHttpEvent(ev); });
    KM_SetObjKey("H1xStream");
}

H1xStream::~H1xStream()
{
    
}

KMError H1xStream::addHeader(std::string name, std::string value)
{
    return outgoing_message_.addHeader(std::move(name), std::move(value));
}

KMError H1xStream::sendRequest(const std::string &method, const std::string &url, const std::string &ver)
{
    is_server_ = false;
    auto req = buildRequest(method, url, ver);
    return sendHeaders(req);
}

KMError H1xStream::sendResponse(int status_code, const std::string &desc, const std::string &ver)
{
    is_server_ = true;
    auto rsp = buildResponse(status_code, desc, ver);
    return sendHeaders(rsp);
}

KMError H1xStream::setHttpParser(HttpParser::Impl&& parser)
{
    incoming_parser_.reset();
    incoming_parser_ = std::move(parser);
    incoming_parser_.setDataCallback([this] (KMBuffer &buf) { onHttpData(buf); });
    incoming_parser_.setEventCallback([this] (HttpEvent ev) { onHttpEvent(ev); });
    
    if (incoming_parser_.paused()) {
        incoming_parser_.resume();
    }
    
    return KMError::NOERR;
}

std::string H1xStream::buildRequest(const std::string &method, const std::string &url, const std::string &ver)
{
    auto req = outgoing_message_.buildHeader(method, url, ver);
    return req;
}

std::string H1xStream::buildResponse(int status_code, const std::string &desc, const std::string &ver)
{
    auto rsp = outgoing_message_.buildHeader(status_code, desc, ver, incoming_parser_.getMethod());
    return rsp;
}

KMError H1xStream::sendHeaders(const std::string &headers)
{
    if (sender_) {
        auto ret = sender_(headers.data(), headers.size());
        if (ret > 0) {
            bool end_stream = !outgoing_message_.hasBody();
            if (end_stream) {
                onOutgoingComplete();
            }
            return KMError::NOERR;
        } else if (ret < 0) {
            return KMError::SOCK_ERROR;
        } else {
            return KMError::FAILED;
        }
    } else {
        return KMError::INVALID_STATE;
    }
}

int H1xStream::sendData(const void* data, size_t len)
{
    auto ret = outgoing_message_.sendData(data, len);
    if (ret >= 0) {
        if (outgoing_message_.isComplete()) {
            onOutgoingComplete();
        }
    }
    return ret;
}

int H1xStream::sendData(const KMBuffer &buf)
{
    auto ret = outgoing_message_.sendData(buf);
    if (ret >= 0) {
        if (outgoing_message_.isComplete()) {
            onOutgoingComplete();
        }
    }
    return ret;
}

KMError H1xStream::handleInputData(uint8_t *src, size_t len)
{
    int bytes_used = incoming_parser_.parse((char*)src, len);
    if (bytes_used != len) {
        
    }
    return KMError::NOERR;
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

void H1xStream::onError(KMError err)
{
    if(error_cb_) error_cb_(err);
}

void H1xStream::reset()
{
    outgoing_message_.reset();
    incoming_parser_.reset();
}

KMError H1xStream::close()
{
    return KMError::NOERR;
}

