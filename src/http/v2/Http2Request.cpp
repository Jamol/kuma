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
    
}

KMError Http2Request::setSslFlags(uint32_t ssl_flags)
{
    return KMError::NOERR;
}

void Http2Request::addHeader(std::string name, std::string value)
{
    transform(name.begin(), name.end(), name.begin(), ::tolower);
    HttpRequest::Impl::addHeader(std::move(name), std::move(value));
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
    auto &connMgr = H2ConnectionMgr::getRequestConnMgr(ssl_flags != SSL_NONE);
    conn_ = connMgr.getConnection(key);
    if (!conn_) {
        conn_.reset(new H2Connection::Impl(loop_));
        conn_->setConnectionKey(key);
        conn_->setSslFlags(ssl_flags);
        connMgr.addConnection(key, conn_);
        setState(State::CONNECTING);
        return conn_->connect(uri_.getHost(), port, [this] (KMError err) { onConnect(err); });
    } else if (!conn_->isReady()) {
        return KMError::INVALID_STATE;
    }
    sendHeaders();
    return KMError::NOERR;
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
    if(header_map_.find("accept") == header_map_.end()) {
        addHeader("accept", "*/*");
    }
    if(header_map_.find("content-type") == header_map_.end()) {
        addHeader("content-type", "application/octet-stream");
    }
    if(header_map_.find("user-agent") == header_map_.end()) {
        addHeader("user-agent", UserAgent);
    }
    if(header_map_.find("cache-control") == header_map_.end()) {
        addHeader("cache-control", "no-cache");
    }
    if(header_map_.find("pragma") == header_map_.end()) {
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
    for (auto it : header_map_) {
        headers.emplace_back(std::make_pair(it.first, it.second));
        headers_size += it.first.size() + it.second.size();
    }
    return headers_size;
}

void Http2Request::sendHeaders()
{
    stream_ = conn_->createStream();
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
    setState(State::SENDING_HEADER);
    
    HeaderVector headers;
    size_t headersSize = buildHeaders(headers);
    bool endStream = !has_content_length_ && !is_chunked_;
    stream_->sendHeaders(conn_.get(), headers, headersSize, endStream);
    if (endStream) {
        setState(State::RECVING_RESPONSE);
    }
    stream_->sendWindowUpdate(conn_.get(), H2_MAX_WINDOW_SIZE);
}

void Http2Request::onConnect(KMError err)
{
    if(err != KMError::NOERR) {
        if(error_cb_) error_cb_(err);
        return ;
    }
    sendHeaders();
}

int Http2Request::sendData(const uint8_t* data, size_t len)
{
    int ret = 0;
    if (data && len) {
        size_t send_len = len;
        if (has_content_length_ && body_bytes_sent_ + send_len > content_length_) {
            send_len = content_length_ - body_bytes_sent_;
        }
        ret = stream_->sendData(conn_.get(), data, send_len, false);
        if (ret > 0) {
            body_bytes_sent_ += ret;
        }
    }
    bool endStream = (!data && !len) || (has_content_length_ && body_bytes_sent_ >= content_length_);
    if (endStream) {
        stream_->sendData(conn_.get(), nullptr, 0, true);
        setState(State::RECVING_RESPONSE);
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

void Http2Request::onData(uint8_t *data, size_t len, bool endSteam)
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
    if (error_cb_) {
        error_cb_(KMError::FAILED);
    }
}

void Http2Request::onWrite()
{
    if(write_cb_) write_cb_(KMError::NOERR);
}

KMError Http2Request::close()
{
    if (stream_) {
        stream_->close(conn_.get());
        stream_.reset();
    }
    conn_.reset();
    return KMError::NOERR;
}
