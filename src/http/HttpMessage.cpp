/* Copyright © 2017, Fengping Bao <jamol@live.com>
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

#include "HttpMessage.h"
#include <sstream>

using namespace kuma;

void HttpMessage::addHeader(std::string name, std::string value)
{
    if(!name.empty()) {
        header_map_.emplace(std::move(name), std::move(value));
    }
}

void HttpMessage::addHeader(std::string name, uint32_t value)
{
    addHeader(std::move(name), std::to_string(value));
}

bool HttpMessage::hasHeader(const std::string &name) const
{
    return header_map_.find(name) != header_map_.end();
}

void HttpMessage::processHeaders()
{
    auto it = header_map_.find(strContentLength);
    if (it != header_map_.end()) {
        has_content_length_ = true;
        content_length_ = std::stol(it->second);
    } else {
        has_content_length_ = false;
        content_length_ = 0;
    }
    
    it = header_map_.find(strTransferEncoding);
    if (it != header_map_.end()) {
        is_chunked_ = is_equal(strChunked, it->second);
    } else {
        is_chunked_ = false;
    }
    
    has_body_ = is_chunked_ || (has_content_length_ && content_length_ > 0);
}

std::string HttpMessage::buildMessageHeader(const std::string &method, const std::string &url, const std::string &ver)
{
    processHeaders();
    std::string req = method + " " + url + " " + (!ver.empty()?ver:VersionHTTP1_1);
    req += "\r\n";
    for (auto &kv : header_map_) {
        req += kv.first + ": " + kv.second + "\r\n";
    }
    req += "\r\n";
    return req;
}

std::string HttpMessage::buildMessageHeader(int status_code, const std::string &desc, const std::string &ver)
{
    processHeaders();
    std::string rsp = (!ver.empty()?ver:VersionHTTP1_1) + " " + std::to_string(status_code);
    if (!desc.empty()) {
        rsp += " " + desc;
    }
    rsp += "\r\n";
    for (auto &kv : header_map_) {
        rsp += kv.first + ": " + kv.second + "\r\n";
    }
    rsp += "\r\n";

    has_body_ = has_body_ || !((100 <= status_code && status_code <= 199) ||
                               204 == status_code || 304 == status_code);
    return rsp;
}

int HttpMessage::sendData(const void* data, size_t len)
{
    if(is_chunked_) {
        return sendChunk(data, len);
    }
    if(!data || 0 == len) {
        return 0;
    }
    int ret = sender_(data, len);
    if(ret > 0) {
        body_bytes_sent_ += ret;
        if (body_bytes_sent_ >= content_length_) {
            completed_ = true;
        }
    }
    return ret;
}

int HttpMessage::sendChunk(const void* data, size_t len)
{
    if(nullptr == data && 0 == len) { // chunk end
        static const std::string _chunk_end_token_ = "0\r\n\r\n";
        int ret = sender_(_chunk_end_token_.c_str(), _chunk_end_token_.length());
        if(ret > 0) {
            completed_ = true;
            return 0;
        }
        return ret;
    } else {
        std::stringstream ss;
        ss << std::hex << len;
        std::string str;
        ss >> str;
        str += "\r\n";
        iovec iovs[3];
        iovs[0].iov_base = (char*)str.c_str();
        iovs[0].iov_len = str.length();
        iovs[1].iov_base = (char*)data;
        iovs[1].iov_len = len;
        iovs[2].iov_base = (char*)"\r\n";
        iovs[2].iov_len = 2;
        int ret = sender_(iovs, 3);
        if(ret > 0) {
            body_bytes_sent_ += ret;
            return int(len);
        }
        return ret;
    }
}

void HttpMessage::reset()
{
    header_map_.clear();
    has_content_length_ = false;
    content_length_ = 0;
    is_chunked_ = false;
    body_bytes_sent_ = 0;
    completed_ = false;
    has_body_ = false;
}
