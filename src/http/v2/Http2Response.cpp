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
#include <string>

using namespace kuma;

Http2Response::Http2Response(const EventLoopPtr &loop, std::string ver)
: HttpResponse::Impl(std::move(ver)), loop_(loop)
{
    loop_token_.eventLoop(loop);
    KM_SetObjKey("Http2Response");
}

void Http2Response::cleanup()
{
    if (stream_) {
        stream_->close();
        stream_.reset();
    }
    loop_token_.reset();
}

KMError Http2Response::attachStream(H2Connection::Impl* conn, uint32_t stream_id)
{
    stream_ = conn->getStream(stream_id);
    if (!stream_) {
        return KMError::INVALID_STATE;
    }
    stream_->setHeadersCallback([this] (const HeaderVector &headers, bool endSteam) {
        onHeaders(headers, endSteam);
    });
    stream_->setDataCallback([this] (KMBuffer &buf, bool endSteam) {
        onData(buf, endSteam);
    });
    stream_->setRSTStreamCallback([this] (int err) {
        onRSTStream(err);
    });
    stream_->setWriteCallback([this] {
        onWrite();
    });
    return KMError::NOERR;
}

KMError Http2Response::addHeader(std::string name, std::string value)
{
    if(!name.empty()) {
        transform(name.begin(), name.end(), name.begin(), ::tolower);
        if (is_equal("transfer-encoding", name) && is_equal("chunked", value)) {
            is_chunked_ = true;
        } else {
            HttpHeader::addHeader(std::move(name), std::move(value));
        }
        return KMError::NOERR;
    }
    
    return KMError::INVALID_PARAM;
}

KMError Http2Response::sendResponse(int status_code, const std::string& desc, const std::string& ver)
{
    KUMA_INFOXTRACE("sendResponse, status_code="<<status_code);
    setState(State::SENDING_HEADER);
    HeaderVector headers;
    size_t headersSize = buildHeaders(status_code, headers);
    bool endStream = has_content_length_ && content_length_ == 0;
    auto ret = stream_->sendHeaders(headers, headersSize, endStream);
    if (ret == KMError::NOERR) {
        if (endStream) {
            setState(State::COMPLETE);
            auto loop = loop_.lock();
            if (loop) {
                loop->post([this] { notifyComplete(); }, &loop_token_);
            }
        } else {
            setState(State::SENDING_BODY);
            auto loop = loop_.lock();
            if (loop) {
                loop->post([this] { if (write_cb_) write_cb_(KMError::NOERR); }, &loop_token_);
            }
        }
    }
    return ret;
}

int Http2Response::sendData(const void* data, size_t len)
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
        auto loop = loop_.lock();
        if (loop) {
            loop->post([this] { notifyComplete(); }, &loop_token_);
        }
    }
    return ret;
}

int Http2Response::sendData(const KMBuffer &buf)
{
    if (getState() != State::SENDING_BODY) {
        return 0;
    }
    
    auto chain_len = buf.chainLength();
    int ret = 0;
    if (chain_len) {
        size_t send_len = chain_len;
        if (has_content_length_ && body_bytes_sent_ + send_len > content_length_) {
            send_len = content_length_ - body_bytes_sent_;
        }
        ret = stream_->sendData(buf, false);
        if (ret > 0) {
            body_bytes_sent_ += ret;
        }
    }
    bool endStream = (!chain_len) || (has_content_length_ && body_bytes_sent_ >= content_length_);
    if (endStream) { // end stream
        stream_->sendData(nullptr, 0, true);
        setState(State::COMPLETE);
        auto loop = loop_.lock();
        if (loop) {
            loop->post([this] { notifyComplete(); }, &loop_token_);
        }
    }
    return ret;
}

void Http2Response::checkHeaders()
{
    
}

size_t Http2Response::buildHeaders(int status_code, HeaderVector &headers)
{
    HttpHeader::processHeader(status_code);
    size_t headers_size = 0;
    std::string str_status_code = std::to_string(status_code);
    headers.emplace_back(std::make_pair(H2HeaderStatus, str_status_code));
    headers_size += H2HeaderStatus.size() + str_status_code.size();
    for (auto const &kv : header_vec_) {
        headers.emplace_back(kv.first, kv.second);
        headers_size += kv.first.size() + kv.second.size();
    }
    return headers_size;
}

const std::string& Http2Response::getParamValue(const std::string &name) const {
    return EmptyString;
}

const std::string& Http2Response::getHeaderValue(const std::string &name) const {
    for (auto const &kv : req_headers_) {
        if (is_equal(kv.first, name)) {
            return kv.second;
        }
    }
    return EmptyString;
}

void Http2Response::forEachHeader(const EnumerateCallback &cb) const {
    for (auto &kv : req_headers_) {
        if (!cb(kv.first, kv.second)) {
            break;
        }
    }
}

void Http2Response::onHeaders(const HeaderVector &headers, bool end_stream)
{
    if (headers.empty()) {
        return;
    }
    std::string str_cookie;
    for (auto const &kv : headers) {
        auto const &name = kv.first;
        auto const &value = kv.second;
        if (!name.empty()) {
            if (name[0] == ':') { // pseudo header
                if (name == H2HeaderMethod) {
                    req_method_ = value;
                } else if (name == H2HeaderAuthority) {
                    req_headers_.emplace_back(strHost, value);
                } else if (name == H2HeaderPath) {
                    req_path_ = value;
                }
            } else {
                if (is_equal(name, H2HeaderCookie)) {
                    // reassemble cookie
                    if (!str_cookie.empty()) {
                        str_cookie += "; ";
                    }
                    str_cookie += value;
                } else {
                    req_headers_.emplace_back(name, value);
                }
            }
        }
    }
    if (!str_cookie.empty()) {
        req_headers_.emplace_back(strCookie, std::move(str_cookie));
    }
    DESTROY_DETECTOR_SETUP();
    if (header_cb_) header_cb_();
    DESTROY_DETECTOR_CHECK_VOID();
    if (end_stream) {
        setState(State::WAIT_FOR_RESPONSE);
        if (request_cb_) request_cb_();
    }
}

void Http2Response::onData(KMBuffer &buf, bool end_stream)
{
    DESTROY_DETECTOR_SETUP();
    if (data_cb_ && buf.chainLength() > 0) data_cb_(buf);
    DESTROY_DETECTOR_CHECK_VOID();
    
    if (end_stream) {
        setState(State::WAIT_FOR_RESPONSE);
        if (request_cb_) request_cb_();
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
