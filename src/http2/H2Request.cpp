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

#include <sstream>

using namespace kuma;

H2Request::H2Request(EventLoopImpl* loop)
: loop_(loop)
{
    
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
    char ip_buf[128];
    km_resolve_2_ip(uri_.getHost().c_str(), ip_buf, sizeof(ip_buf));
    std::stringstream ss;
    ss << ip_buf << ":" << port;
    std::string key = ss.str();
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
    return strEmpty;
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
        addHeader("user-agent", strUserAgent);
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
    headers.emplace_back(std::make_pair(strHeaderMethod, method_));
    headers_size += strHeaderMethod.size() + method_.size();
    headers.emplace_back(std::make_pair(strHeaderScheme, uri_.getScheme()));
    headers_size += strHeaderScheme.size() + uri_.getScheme().size();
    std::string path = uri_.getPath();
    if(!uri_.getQuery().empty()) {
        path = "?" + uri_.getQuery();
    }
    if(!uri_.getFragment().empty()) {
        path = "#" + uri_.getFragment();
    }
    headers_size += strHeaderPath.size() + path.size();
    headers.emplace_back(std::make_pair(strHeaderPath, path));
    headers.emplace_back(std::make_pair(strHeaderAuthority, uri_.getHost()));
    for (auto it : header_map_) {
        headers.emplace_back(std::make_pair(it.first, it.second));
        headers_size += it.first.size() + it.second.size();
    }
    return headers_size;
}

void H2Request::sendSettings()
{
    ParamVector params;
    params.push_back(std::make_pair(HEADER_TABLE_SIZE, 4096));
}

void H2Request::sendHeaders()
{
    stream_ = conn_->createStream();
    setState(State::SENDING_HEADER);
    
    ParamVector params;
    params.push_back(std::make_pair(HEADER_TABLE_SIZE, 4096));
    conn_->sendSetting(stream_, params);
    
    HeaderVector headers;
    size_t headers_size = buildHeaders(headers);
    conn_->sendHeaders(stream_, headers, headers_size);
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
    return 0;
}

int H2Request::close()
{
    return KUMA_ERROR_NOERR;
}
