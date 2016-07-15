/* Copyright (c) 2014, Fengping Bao <jamol@live.com>
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include "IHttpRequest.h"
#include "util/kmtrace.h"
#include "util/util.h"

#include <sstream>
#include <iterator>

using namespace kuma;

//////////////////////////////////////////////////////////////////////////

const char* IHttpRequest::getObjKey() const
{
    return "HttpRequest";
}

void IHttpRequest::addHeader(const std::string& name, const std::string& value)
{
    if(!name.empty()) {
        header_map_[name] = value;
    }
}

void IHttpRequest::addHeader(const std::string& name, uint32_t value)
{
    std::stringstream ss;
    ss << value;
    addHeader(name, ss.str());
}

int IHttpRequest::sendRequest(const std::string& method, const std::string& url, const std::string& ver)
{
    if (getState() != State::IDLE && getState() != State::WAIT_FOR_REUSE) {
        return KUMA_ERROR_INVALID_STATE;
    }
    method_ = method;
    url_ = url;
    version_ = ver;
    if(!uri_.parse(url)) {
        return KUMA_ERROR_INVALID_PARAM;
    }
    checkHeaders();
    return sendRequest();
}

void IHttpRequest::reset()
{
    header_map_.clear();
    body_bytes_sent_ = 0;
}
