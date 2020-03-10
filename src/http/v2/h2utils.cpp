/* Copyright © 2014-2017, Fengping Bao <jamol@live.com>
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

#include "h2utils.h"
#include "util/util.h"

KUMA_NS_BEGIN

bool processH2RequestHeaders(const HeaderVector &h2_headers, std::string &method, std::string &path, HeaderVector &req_headers)
{
    if (h2_headers.empty()) {
        return false;
    }
    std::string str_cookie;
    for (auto const &kv : h2_headers) {
        auto const &name = kv.first;
        auto const &value = kv.second;
        if (!name.empty()) {
            if (name[0] == ':') { // pseudo header
                if (name == H2HeaderMethod) {
                    method = value;
                } else if (name == H2HeaderAuthority) {
                    req_headers.emplace_back(strHost, value);
                } else if (name == H2HeaderPath) {
                    path = value;
                } else {
                    req_headers.emplace_back(name, value);
                }
            } else {
                if (kev::is_equal(name, H2HeaderCookie)) {
                    // reassemble cookie
                    if (!str_cookie.empty()) {
                        str_cookie += "; ";
                    }
                    str_cookie += value;
                } else {
                    req_headers.emplace_back(name, value);
                }
            }
        }
    }
    if (!str_cookie.empty()) {
        req_headers.emplace_back(strCookie, std::move(str_cookie));
    }
    
    return true;
}

bool processH2ResponseHeaders(const HeaderVector &h2_headers, int &status_code, HeaderVector &rsp_headers)
{
    if (h2_headers.empty()) {
        return false;
    }
    if (!kev::is_equal(h2_headers[0].first, H2HeaderStatus)) {
        return false;
    }
    status_code = std::stoi(h2_headers[0].second);
    std::string str_cookie;
    for (auto const &kv : h2_headers) {
        auto const &name = kv.first;
        auto const &value = kv.second;
        if (!name.empty()) {
            if (kev::is_equal(name, H2HeaderCookie)) {
                // reassemble cookie
                if (!str_cookie.empty()) {
                    str_cookie += "; ";
                }
                str_cookie += value;
            } else if (name[0] != ':') {
                rsp_headers.emplace_back(name, value);
            }
        }
    }
    if (!str_cookie.empty()) {
        rsp_headers.emplace_back(strCookie, std::move(str_cookie));
    }
    
    return true;
}

KUMA_NS_END
