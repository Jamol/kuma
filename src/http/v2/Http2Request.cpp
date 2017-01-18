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
#include "http/Uri.h"
#include "H2ConnectionMgr.h"
#include "util/kmtrace.h"

#include <sstream>
#include <algorithm>

using namespace kuma;

Http2Request::Http2Request(EventLoop::Impl* loop, std::string ver)
: HttpRequest::Impl(std::move(ver)), loop_(loop)
{
    KM_SetObjKey("Http2Request");
}

Http2Request::~Http2Request()
{
    for (auto iov : data_list_) {
        delete [] (uint8_t*)iov.iov_base;
    }
    data_list_.clear();
}

KMError Http2Request::setSslFlags(uint32_t ssl_flags)
{
    return KMError::NOERR;
}

void Http2Request::addHeader(std::string name, std::string value)
{
    transform(name.begin(), name.end(), name.begin(), ::tolower);
    if(!name.empty()) {
        if (is_equal("content-length", name)) {
            has_content_length_ = true;
            content_length_ = atol(value.c_str());
        } else if (is_equal("Transfer-Encoding", name) && is_equal("chunked", value)) {
            is_chunked_ = true;
            return; // omit chunked
        }
        req_headers[std::move(name)] = std::move(value);
    }
}

KMError Http2Request::sendRequest()
{
    std::string str_port = uri_.getPort();
    uint16_t port = 80;
    uint32_t ssl_flags = SSL_NONE;
    if (is_equal("https", uri_.getScheme())) {
        ssl_flags = SSL_ENABLE;
        port = 443;
    }
    if(!str_port.empty()) {
        port = std::stoi(str_port);
    }
    std::string key;
    char ip_buf[128];
    if (km_resolve_2_ip(uri_.getHost().c_str(), ip_buf, sizeof(ip_buf)) == 0) {
        std::stringstream ss;
        ss << ip_buf << ":" << port;
        key = ss.str();
    } else {
        key = uri_.getHost() + ":" + std::to_string(port);
    }
    setState(State::CONNECTING);
    auto &connMgr = H2ConnectionMgr::getRequestConnMgr(ssl_flags != SSL_NONE);
    conn_ = connMgr.getConnection(key, uri_.getHost(), port, ssl_flags, loop_);
    if (!conn_) {
        KUMA_ERRXTRACE("sendRequest, failed to get H2Connection, key="<<key);
        return KMError::INVALID_PARAM;
    } else if (conn_->isInSameThread()) {
        return sendRequest_i();
    } else if (!conn_->async([this] { auto err = sendRequest_i(); if (err != KMError::NOERR) { onError(err); }})) {
        KUMA_ERRXTRACE("sendRequest, failed to run in H2Connection, key="<<key);
        return KMError::INVALID_STATE;
    }
    return KMError::NOERR;
}

KMError Http2Request::sendRequest_i()
{
    if (!conn_) {
        return KMError::INVALID_STATE;
    }
    if (!conn_->isReady()) {
        conn_->addConnectListener(getObjId(), [this] (KMError err) { onConnect(err); });
        return KMError::NOERR;
    } else {
        return sendHeaders();
    }
}

const std::string& Http2Request::getHeaderValue(std::string name) const
{
    auto it = rsp_headers_.find(name);
    return it != rsp_headers_.end() ? it->second : EmptyString;
}

void Http2Request::forEachHeader(EnumrateCallback cb)
{
    for (auto &kv : rsp_headers_) {
        cb(kv.first, kv.second);
    }
}

void Http2Request::checkHeaders()
{
    if(req_headers.find("accept") == req_headers.end()) {
        addHeader("accept", "*/*");
    }
    if(req_headers.find("content-type") == req_headers.end()) {
        addHeader("content-type", "application/octet-stream");
    }
    if(req_headers.find("user-agent") == req_headers.end()) {
        addHeader("user-agent", UserAgent);
    }
    if(req_headers.find("cache-control") == req_headers.end()) {
        addHeader("cache-control", "no-cache");
    }
    if(req_headers.find("pragma") == req_headers.end()) {
        addHeader("pragma", "no-cache");
    }
}

size_t Http2Request::buildHeaders(HeaderVector &headers)
{
    size_t headers_size = 0;
    headers.emplace_back(std::make_pair(H2HeaderMethod, method_));
    headers_size += H2HeaderMethod.size() + method_.size();
    headers.emplace_back(std::make_pair(H2HeaderScheme, uri_.getScheme()));
    headers_size += H2HeaderScheme.size() + uri_.getScheme().size();
    std::string path = uri_.getPath();
    if(!uri_.getQuery().empty()) {
        path = "?" + uri_.getQuery();
    }
    if(!uri_.getFragment().empty()) {
        path = "#" + uri_.getFragment();
    }
    headers.emplace_back(std::make_pair(H2HeaderPath, path));
    headers_size += H2HeaderPath.size() + path.size();
    headers.emplace_back(std::make_pair(H2HeaderAuthority, uri_.getHost()));
    headers_size += H2HeaderAuthority.size() + uri_.getHost().size();
    for (auto it : req_headers) {
        headers.emplace_back(std::make_pair(it.first, it.second));
        headers_size += it.first.size() + it.second.size();
    }
    return headers_size;
}

KMError Http2Request::sendHeaders()
{
    stream_ = conn_->createStream();
    stream_->setHeadersCallback([this] (const HeaderVector &headers, bool endHeaders, bool endSteam) {
        onHeaders(headers, endHeaders, endSteam);
    });
    stream_->setDataCallback([this] (void *data, size_t len, bool endSteam) {
        onData(data, len, endSteam);
    });
    stream_->setRSTStreamCallback([this] (int err) {
        onRSTStream(err);
    });
    stream_->setWriteCallback([this] {
        onWrite();
    });
    setState(State::SENDING_HEADER);
    
    HeaderVector headers;
    size_t headersSize = buildHeaders(headers);
    bool endStream = !has_content_length_ && !is_chunked_;
    auto ret = stream_->sendHeaders(headers, headersSize, endStream);
    if (ret == KMError::NOERR) {
        if (endStream) {
            setState(State::RECVING_RESPONSE);
        } else {
            setState(State::SENDING_BODY);
            onWrite(); // should queue in event loop rather than call onWrite directly?
        }
    }
    return ret;
}

void Http2Request::onConnect(KMError err)
{
    if(err != KMError::NOERR) {
        onError(err);
        return ;
    }
    sendHeaders();
}

void Http2Request::onError(KMError err)
{
    if(error_cb_) error_cb_(err);
}

int Http2Request::sendData(const void* data, size_t len)
{
    if (!conn_) {
        return -1;
    }
    if (getState() != State::SENDING_BODY) {
        return 0;
    }
    if (write_blocked_) {
        return 0;
    }
    if (conn_->isInSameThread()) {
        return sendData_i(data, len);
    } else {
        uint8_t *d = nullptr;
        if (data && len) {
            d = new uint8_t[len];
            memcpy(d, data, len);
        }
        conn_->async([=] { sendData_i(d, len, true); });
        return int(len);
    }
}

int Http2Request::sendData_i(const void* data, size_t len, bool newData)
{
    if (getState() != State::SENDING_BODY) {
        if (newData && len > 0) {
            delete [] (uint8_t*)data;
        }
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
    if (endStream) {
        stream_->sendData(nullptr, 0, true);
        setState(State::RECVING_RESPONSE);
    }
    if (newData && len > 0) {
        if (ret == 0) {// write blocked
            write_blocked_ = true;
            iovec iov;
            iov.iov_base = (char*) data;
            iov.iov_len = len;
            data_list_.push_back(iov);
            ret = int(len);
        } else {
            delete [] (uint8_t*)data;
        }
    }
    return ret;
}

void Http2Request::onHeaders(const HeaderVector &headers, bool endHeaders, bool endSteam)
{
    if (headers.empty()) {
        return;
    }
    if (!is_equal(headers[0].first, H2HeaderStatus)) {
        return;
    }
    status_code_ = std::stoi(headers[0].second);
    for (size_t i = 1; i < headers.size(); ++i) {
        rsp_headers_.emplace(headers[i].first, headers[i].second);
    }
    if (endHeaders) {
        DESTROY_DETECTOR_SETUP();
        if (header_cb_) header_cb_();
        DESTROY_DETECTOR_CHECK_VOID();
    }
    if (endSteam) {
        setState(State::COMPLETE);
        if (response_cb_) response_cb_();
    }
}

void Http2Request::onData(void *data, size_t len, bool endSteam)
{
    DESTROY_DETECTOR_SETUP();
    if (data_cb_ && len > 0) data_cb_(data, len);
    DESTROY_DETECTOR_CHECK_VOID();
    
    if (endSteam && response_cb_) {
        setState(State::COMPLETE);
        response_cb_();
    }
}

void Http2Request::onRSTStream(int err)
{
    onError(KMError::FAILED);
}

void Http2Request::onWrite()
{
    while (!data_list_.empty()) {
        auto iov = data_list_.front();
        int ret = sendData_i(iov.iov_base, iov.iov_len);
        if (ret > 0) {
            data_list_.pop_front();
            delete [] (uint8_t*)iov.iov_base;
        } else if (ret == 0) {
            return;
        } else {
            onError(KMError::FAILED);
            return;
        }
    }
    write_blocked_ = false;
    if(write_cb_) write_cb_(KMError::NOERR);
}

KMError Http2Request::close()
{
    if (conn_) {
        conn_->sync([this] { close_i(); });
    }
    conn_.reset();
    return KMError::NOERR;
}

void Http2Request::close_i()
{
    if (getState() == State::CONNECTING && conn_) {
        conn_->removeConnectListener(getObjId());
    }
    if (stream_) {
        stream_->close();
        stream_.reset();
    }
}
