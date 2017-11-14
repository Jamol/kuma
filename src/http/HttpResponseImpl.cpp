/* Copyright (c) 2014, Fengping Bao <jamol@live.com>
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

#include "HttpResponseImpl.h"
#include "EventLoopImpl.h"
#include "util/kmtrace.h"

#include <iterator>

using namespace kuma;

static const std::string str_content_type = "Content-Type";
static const std::string str_content_length = "Content-Length";
static const std::string str_transfer_encoding = "Transfer-Encoding";
static const std::string str_chunked = "chunked";
//////////////////////////////////////////////////////////////////////////
HttpResponse::Impl::Impl(std::string ver)
: version_(std::move(ver))
{
    
}

HttpResponse::Impl::~Impl()
{
    
}

void HttpResponse::Impl::addHeader(std::string name, uint32_t value)
{
    addHeader(std::move(name), std::to_string(value));
}

KMError HttpResponse::Impl::sendResponse(int status_code, const std::string& desc)
{
    if (getState() != State::WAIT_FOR_RESPONSE) {
        return KMError::INVALID_STATE;
    }
    checkHeaders();
    return sendResponse(status_code, desc, version_);
}
/*
int HttpResponse::Impl::sendData(const KMBuffer &buf)
{
    int bytes_sent = 0;
    for (auto it = buf.begin(); it != buf.end(); ++it) {
        auto ret = sendData(it->readPtr(), it->length());
        if (ret < 0) {
            return ret;
        }
        bytes_sent += ret;
        if (static_cast<size_t>(ret) < it->length()) {
            return bytes_sent;
        }
    }
    return bytes_sent;
}
*/
void HttpResponse::Impl::reset()
{
    
}

void HttpResponse::Impl::notifyComplete()
{
    if(response_cb_) response_cb_();
}
