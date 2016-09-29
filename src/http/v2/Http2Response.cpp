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

#include <algorithm>

using namespace kuma;

Http2Response::Http2Response(EventLoop::Impl* loop, std::string ver)
: HttpResponse::Impl(std::move(ver)), loop_(loop)
{
    KM_SetObjKey("Http2Response");
}

void Http2Response::cleanup()
{
    if (stream_) {
        stream_->close();
        stream_.reset();
    }
}

KMError Http2Response::attachStream(H2Connection::Impl* conn, uint32_t streamId)
{
    //stream_ = conn->createStream(streamId);
    stream_ = conn->getStream(streamId);
    if (!stream_) {
        return KMError::INVALID_STATE;
    }
    stream_->setHeadersCallback([this] (const HeaderVector &headers, bool endHeaders, bool endSteam) {
        onHeaders(headers, endHeaders, endSteam);
    });
    stream_->setDataCallback([this] (uint8_t *data, size_t len, bool endSteam) {
        onData(data, len, endSteam);
    });
    stream_->setRSTStreamCallback([this] (int err) {
        onRSTStream(err);
    });
    stream_->setWriteCallback([this] {
        onWrite();
    });
    return KMError::NOERR;
}

void Http2Response::addHeader(std::string name, std::string value)
{
    transform(name.begin(), name.end(), name.begin(), ::tolower);
    HttpResponse::Impl::addHeader(std::move(name), std::move(value));
}

KMError Http2Response::sendResponse(int status_code, const std::string& desc, const std::string& ver)
{
    KUMA_INFOXTRACE("sendResponse, status_code="<<status_code);
    setState(State::SENDING_HEADER);
    HeaderVector headers;
    size_t headersSize = buildHeaders(status_code, headers);
    bool endStream = !has_content_length_ && !is_chunked_;
    stream_->sendHeaders(headers, headersSize, endStream);
    if (endStream) {
        setState(State::COMPLETE);
        loop_->queueInEventLoop([this] { notifyComplete(); });
    } else {
        setState(State::SENDING_BODY);
        loop_->queueInEventLoop([this] { if (write_cb_) write_cb_(KMError::NOERR); });
    }
    return KMError::NOERR;
}

int Http2Response::sendData(const uint8_t* data, size_t len)
{
    if (getState() != State::SENDING_BODY) {
        return 0;
    }

    int ret = 0;
    if (data && len) {
        size_t send_len = len;
        if (has_content_length_ && body_bytes_sent_ + send_len > content_length_) {
            send_len = content_length_ - body_bytes_sent_;
        }
        ret = stream_->sendData(data, send_len, false);
        if (ret > 0) {
            body_bytes_sent_ += ret;
        }
    }
    bool endStream = (!data && !len) || (has_content_length_ && body_bytes_sent_ >= content_length_);
    if (endStream) { // end stream
        stream_->sendData(nullptr, 0, true);
        setState(State::COMPLETE);
        loop_->queueInEventLoop([this] { notifyComplete(); });
    }
    return ret;
}

size_t Http2Response::buildHeaders(int status_code, HeaderVector &headers)
{
    size_t headers_size = 0;
    std::string str_status_code = std::to_string(status_code);
    headers.emplace_back(std::make_pair(H2HeaderStatus, str_status_code));
    headers_size += H2HeaderMethod.size() + str_status_code.size();
    for (auto it : header_map_) {
        headers.emplace_back(std::make_pair(it.first, it.second));
        headers_size += it.first.size() + it.second.size();
    }
    return headers_size;
}

void Http2Response::onHeaders(const HeaderVector &headers, bool endheaders, bool endSteam)
{
    if (headers.empty()) {
        return;
    }
    for (int i=0; i<headers.size(); ++i) {
        auto &kv = headers[i];
        auto &name = kv.first;
        auto &value = kv.second;
        if (!name.empty()) {
            if (name[0] == ':') { // pseudo header
                if (name == H2HeaderMethod) {
                    http_parser_.setMethod(value);
                } else if (name == H2HeaderAuthority) {
                    http_parser_.addHeaderValue("host", value);
                } else if (name == H2HeaderPath) {
                    http_parser_.setUrlPath(std::move(value));
                }
            } else {
                http_parser_.addHeaderValue(std::move(name), std::move(value));
            }
        }
    }
    if (endheaders) {
        DESTROY_DETECTOR_SETUP();
        if (header_cb_) header_cb_();
        DESTROY_DETECTOR_CHECK_VOID();
    }
    if (endSteam) {
        setState(State::WAIT_FOR_RESPONSE);
        if (request_cb_) request_cb_();
    }
}

void Http2Response::onData(uint8_t *data, size_t len, bool endSteam)
{
    DESTROY_DETECTOR_SETUP();
    if (data_cb_ && len > 0) data_cb_(data, len);
    DESTROY_DETECTOR_CHECK_VOID();
    
    if (endSteam && response_cb_) {
        setState(State::COMPLETE);
        response_cb_();
    }
}

void Http2Response::onRSTStream(int err)
{
    KUMA_INFOXTRACE("onRSTStream, body_bytes_sent="<<body_bytes_sent_);
    if (error_cb_) {
        error_cb_(KMError::FAILED);
    }
}

void Http2Response::onWrite()
{
    if(write_cb_) write_cb_(KMError::NOERR);
}

KMError Http2Response::close()
{
    KUMA_INFOXTRACE("close");
    cleanup();
    setState(State::CLOSED);
    return KMError::NOERR;
}
