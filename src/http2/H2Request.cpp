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

#include "H2Request.h"
#include "Uri.h"
#include "H2ConnectionMgr.h"
#include "kmtrace.h"

#include <sstream>

using namespace kuma;

H2Request::H2Request(EventLoopImpl* loop)
: loop_(loop)
{
    KM_SetObjKey("H2Request");
}

H2Request::~H2Request()
{
    if(destroy_flag_ptr_) {
        *destroy_flag_ptr_ = true;
    }
}

int H2Request::setSslFlags(uint32_t ssl_flags)
{
    return KUMA_ERROR_NOERR;
}

int H2Request::sendRequest()
{
    std::string str_port = uri_.getPort();
    uint16_t port = 80;
    uint32_t ssl_flags = SSL_NONE;
    if(!str_port.empty()) {
        port = atoi(str_port.c_str());
    } else if(is_equal("https", uri_.getScheme())) {
        port = 443;
        ssl_flags = SSL_ENABLE;
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
        conn_.reset(new H2ConnectionImpl(loop_));
        conn_->setConnectionKey(key);
        conn_->setSslFlags(ssl_flags);
        connMgr.addConnection(key, conn_);
        setState(State::CONNECTING);
        return conn_->connect(uri_.getHost(), port, [this] (int err) { onConnect(err); });
    } else if (!conn_->isReady()) {
        return KUMA_ERROR_INVALID_STATE;
    }
    sendHeaders();
    return KUMA_ERROR_NOERR;
}

const std::string& H2Request::getHeaderValue(const char* name) const
{
    return EmptyString;
}

void H2Request::forEachHeader(EnumrateCallback cb)
{
    
}

void H2Request::checkHeaders()
{
    if(header_map_.find("Accept") == header_map_.end()) {
        addHeader("accept", "*/*");
    }
    if(header_map_.find("Content-Type") == header_map_.end()) {
        addHeader("content-type", "application/octet-stream");
    }
    if(header_map_.find("User-Agent") == header_map_.end()) {
        addHeader("user-agent", UserAgent);
    }
    if(header_map_.find("cache-control") == header_map_.end()) {
        addHeader("cache-control", "no-cache");
    }
    if(header_map_.find("pragma") == header_map_.end()) {
        addHeader("pragma", "no-cache");
    }
}

size_t H2Request::buildHeaders(HeaderVector &headers)
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

void H2Request::sendHeaders()
{
    stream_ = conn_->createStream();
    stream_->setHeadersCallback([this] (const HeaderVector &headers, bool endSteam) {
        onHeaders(headers, endSteam);
    });
    stream_->setDataCallback([this] (uint8_t *data, size_t len, bool endSteam) {
        onData(data, len, endSteam);
    });
    stream_->setRSTStreamCallback([this] (int err) {
        onRSTStream(err);
    });
    setState(State::SENDING_HEADER);
    
    HeaderVector headers;
    size_t headersSize = buildHeaders(headers);
    bool endStream = !has_content_length_ && !is_chunked_;
    stream_->sendHeaders(conn_, headers, headersSize, endStream);
    if (endStream) {
        setState(State::RECVING_RESPONSE);
    }
}

void H2Request::onConnect(int err)
{
    if(err != KUMA_ERROR_NOERR) {
        if(cb_error_) cb_error_(err);
        return ;
    }
    sendHeaders();
}

int H2Request::sendData(const uint8_t* data, size_t len)
{
    body_bytes_sent_ += len;
    bool endStream = is_chunked_ ? !data : body_bytes_sent_ >= content_length_;
    int ret = stream_->sendData(conn_, data, len, endStream);
    if (endStream) {
        setState(State::RECVING_RESPONSE);
    }
    return ret;
}

void H2Request::onHeaders(const HeaderVector &headers, bool endSteam)
{
    bool destroyed = false;
    KUMA_ASSERT(nullptr == destroy_flag_ptr_);
    destroy_flag_ptr_ = &destroyed;
    if (cb_header_) cb_header_();
    if(destroyed) {
        return ;
    }
    destroy_flag_ptr_ = nullptr;
    if (endSteam) {
        setState(State::COMPLETE);
        if (cb_response_) cb_response_();
    }
}

void H2Request::onData(uint8_t *data, size_t len, bool endSteam)
{
    bool destroyed = false;
    KUMA_ASSERT(nullptr == destroy_flag_ptr_);
    destroy_flag_ptr_ = &destroyed;
    if (cb_data_) cb_data_(data, len);
    if(destroyed) {
        return ;
    }
    destroy_flag_ptr_ = nullptr;
    
    if (endSteam && cb_response_) {
        setState(State::COMPLETE);
        cb_response_();
    }
}

void H2Request::onRSTStream(int err)
{
    
}

int H2Request::close()
{
    if (stream_) {
        stream_->close(conn_);
        stream_.reset();
    }
    conn_.reset();
    return KUMA_ERROR_NOERR;
}
