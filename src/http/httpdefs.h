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

#ifndef __HTTPDEFS_H__
#define __HTTPDEFS_H__

#include "kmdefs.h"
#include <string>
#include <vector>
#include <map>
#include <functional>
#include <string.h>

KUMA_NS_BEGIN

const std::string VersionHTTP2_0 = "HTTP/2.0";
const std::string VersionHTTP1_1 = "HTTP/1.1";
const std::string EmptyString = "";
const std::string UserAgent = "kuma 1.0";

const std::string strUserAgent = "User-Agent";
const std::string strContentType = "Content-Type";
const std::string strContentLength = "Content-Length";
const std::string strTransferEncoding = "Transfer-Encoding";
const std::string strChunked = "chunked";
const std::string strCacheControl = "Cache-Control";
const std::string strCookie = "Cookie";
const std::string strHost = "Host";
const std::string strUpgrade = "Upgrade";
const std::string strAcceptEncoding = "Accept-Encoding";
const std::string strContentEncoding = "Content-Encoding";
const std::string strProxyAuthenticate = "Proxy-Authenticate";
const std::string strProxyAuthorization = "Proxy-Authorization";
const std::string strProxyConnection = "Proxy-Connection";

using KeyValuePair = std::pair<std::string, std::string>;
using KeyValueList = std::vector<KeyValuePair>;
using HeaderVector = KeyValueList;
using HttpBody = std::vector<uint8_t>;

struct CaseIgnoreLess
{
    bool operator()(const std::string &lhs, const std::string &rhs) const {
        return strcasecmp(lhs.c_str(), rhs.c_str()) < 0;
    }
};
typedef std::map<std::string, std::string, CaseIgnoreLess> HeaderMap;

KUMA_NS_END

#endif
