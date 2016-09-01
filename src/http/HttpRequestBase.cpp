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

#include "HttpRequestBase.h"
#include "util/kmtrace.h"
#include "util/util.h"

#include <sstream>
#include <iterator>

using namespace kuma;

//////////////////////////////////////////////////////////////////////////
//
void HttpRequestBase::addHeader(std::string name, std::string value)
{
    if(!name.empty()) {
        if (is_equal("Content-Length", name)) {
            has_content_length_ = true;
            content_length_ = atol(value.c_str());
        } else if (is_equal("Transfer-Encoding", name) && is_equal("chunked", value)) {
            is_chunked_ = true;
            if (!isVersion1_1()) {
                return; // omit chunked
            }
        }
        header_map_[std::move(name)] = std::move(value);
    }
}

void HttpRequestBase::addHeader(std::string name, uint32_t value)
{
    addHeader(std::move(name), std::to_string(value));
}

KMError HttpRequestBase::sendRequest(std::string method, std::string url, std::string ver)
{
    if (getState() != State::IDLE && getState() != State::WAIT_FOR_REUSE) {
        return KMError::INVALID_STATE;
    }
    method_ = std::move(method);
    url_ = std::move(url);
    version_ = std::move(ver);
    if(!uri_.parse(url_)) {
        return KMError::INVALID_PARAM;
    }
    checkHeaders();
    return sendRequest();
}

void HttpRequestBase::reset()
{
    header_map_.clear();
    has_content_length_ = false;
    content_length_ = 0;
    is_chunked_ = false;
    body_bytes_sent_ = 0;
}
