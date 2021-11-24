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

#include "Uri.h"

using namespace kuma;

//////////////////////////////////////////////////////////////////////////
Uri::Uri()
{
    
}

Uri::Uri(const std::string& url)
{
    parse(url);
}

Uri::~Uri()
{
    
}

bool Uri::parse(const std::string& url)
{
    if(url.empty()) {
        return false;
    }
    auto pos = url.find("://");
    if(pos != std::string::npos) {
        scheme_.assign(url.begin(), url.begin()+pos);
        pos += 3;
    } else {
        pos = 0;
    }
    if (!parse_host_port(url, pos, host_, port_)) {
        return false;
    }
    if (pos == std::string::npos || pos >= url.size()) { // only host and port
        return true;
    }
    if (url[pos] == '/') { // path
        auto bpos = pos++;
        for (; pos<url.size(); ++pos) {
            if (url[pos] == '?' || url[pos] == '#') {
                break;
            }
        }
        path_.assign(url.begin()+bpos, url.begin()+pos);
        if (pos >= url.size()) {
            return true;
        }
    }
    if (url[pos] == '?') { // query
        auto bpos = ++pos;
        pos = url.find('#', pos);
        if (pos == std::string::npos) {
            query_.assign(url.begin()+bpos, url.end());
            return true;
        } else {
            query_.assign(url.begin()+bpos, url.begin()+pos);
        }
    }
    if (url[pos] == '#') { // fragment
        fragment_.assign(url.begin()+pos+1, url.end());
    }
    return true;
}

bool Uri::parse_host_port(const std::string& url, std::string::size_type &pos, std::string& host, std::string& port)
{
    host.clear();
    port.clear();
    for (; pos<url.size(); ++pos) {
        if (url[pos] == '[' || url[pos] != ' ') {
            break;
        }
    }
    if (pos >= url.size()) {
        return false;
    }
    auto bpos = pos;
    if(url[pos] == '[') { // IPv6
        ++pos;
        pos = url.find(']', pos);
        if(pos == std::string::npos) {
            return false;
        }
        host.assign(url.begin()+bpos+1, url.begin()+pos);
        ++pos;
    }
    for (; pos<url.size(); ++pos) {
        if (url[pos] == ':' || url[pos] == '/' || url[pos] == '?' || url[pos] == '#') {
            break;
        }
    }
    if (url[bpos] != '[') { // IPv4
        host.assign(url.begin()+bpos, url.begin()+pos);
    }
    if (pos < url.size() && url[pos] == ':') {
        bpos = ++pos;
        for (; pos<url.size(); ++pos) {
            if (url[pos] == '/' || url[pos] == '?' || url[pos] == '#') {
                break;
            }
        }
        port.assign(url.begin()+bpos, url.begin()+pos);
    }
    return true;
}
