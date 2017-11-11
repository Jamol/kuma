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

#include "HttpRequestImpl.h"
#include "util/kmtrace.h"
#include "util/util.h"

#include <sstream>
#include <iterator>

using namespace kuma;

//////////////////////////////////////////////////////////////////////////
//
HttpRequest::Impl::Impl(std::string ver)
: version_(std::move(ver))
{
    
}

void HttpRequest::Impl::addHeader(std::string name, uint32_t value)
{
    addHeader(std::move(name), std::to_string(value));
}

KMError HttpRequest::Impl::sendRequest(std::string method, std::string url)
{
    if (getState() == State::COMPLETE) {
        reset(); // reuse case
    }
    if (getState() != State::IDLE && getState() != State::WAIT_FOR_REUSE) {
        return KMError::INVALID_STATE;
    }
    method_ = std::move(method);
    url_ = std::move(url);
    if(!uri_.parse(url_)) {
        return KMError::INVALID_PARAM;
    }
    checkHeaders();
    return sendRequest();
}

int HttpRequest::Impl::sendData(const KMBuffer &buf)
{
    int bytes_sent = 0;
    for (auto it = buf.begin(); it != buf.end(); ++it) {
        auto ret = sendData(it->readPtr(), it->length());
        if (ret < 0) {
            return ret;
        }
        bytes_sent += static_cast<int>(it->length());
        if (ret < it->length()) {
            return bytes_sent;
        }
    }
    return bytes_sent;
}

std::string HttpRequest::Impl::getCacheKey()
{
    std::string cache_key = uri_.getHost() + uri_.getPath();
    if (!uri_.getQuery().empty()) {
        cache_key += "?";
        cache_key += uri_.getQuery();
    }
    return cache_key;
}

void HttpRequest::Impl::reset()
{
    
}
