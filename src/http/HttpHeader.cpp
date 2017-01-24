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

#include "HttpHeader.h"
#include <sstream>

using namespace kuma;

void HttpHeader::addHeader(std::string name, std::string value)
{
    if(!name.empty()) {
        header_map_.emplace(std::move(name), std::move(value));
    }
}

void HttpHeader::addHeader(std::string name, uint32_t value)
{
    addHeader(std::move(name), std::to_string(value));
}

bool HttpHeader::hasHeader(const std::string &name) const
{
    return header_map_.find(name) != header_map_.end();
}

void HttpHeader::processHeaders()
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

std::string HttpHeader::buildHeader(const std::string &method, const std::string &url, const std::string &ver)
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

std::string HttpHeader::buildHeader(int status_code, const std::string &desc, const std::string &ver)
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

void HttpHeader::reset()
{
    header_map_.clear();
    has_content_length_ = false;
    content_length_ = 0;
    is_chunked_ = false;
    has_body_ = false;
}
