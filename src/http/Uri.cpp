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
        auto path_pos = url.find('/', pos);
        std::string hostport;
        if(path_pos == std::string::npos) {
            hostport.assign(url.begin()+pos, url.end());
        } else {
            hostport.assign(url.begin()+pos, url.begin()+path_pos);
        }
        pos = path_pos;
        parse_host_port(hostport, host_, port_);
    } else {
        pos = 0;
    }
    if (pos == std::string::npos) {
        path_ = "/";
        return true;
    } else if(url.at(pos) != '/') {
        return false;
    }
    // now url[pos] == '/'
    auto query_pos = url.find('?', pos+1);
    if (query_pos == std::string::npos) {
        auto fragment_pos = url.find('#', pos+1);
        if(fragment_pos == std::string::npos) {
            path_.assign(url.begin()+pos, url.end());
        } else {
            path_.assign(url.begin()+pos, url.begin()+fragment_pos);
            ++fragment_pos;
            fragment_.assign(url.begin()+fragment_pos, url.end());
        }
        return true;
    }
    path_.assign(url.begin()+pos, url.begin()+query_pos);
    ++query_pos;
    auto fragment_pos = url.find('#', query_pos);
    if(fragment_pos == std::string::npos) {
        query_.assign(url.begin()+query_pos, url.end());
    } else {
        query_.assign(url.begin()+query_pos, url.begin()+fragment_pos);
        ++fragment_pos;
        fragment_.assign(url.begin()+fragment_pos, url.end());
    }
    return true;
}

bool Uri::parse_host_port(const std::string& hostport, std::string& host, std::string& port)
{
    host.clear();
    port.clear();
    auto pos = hostport.find('[');
    if(pos != std::string::npos) { // ipv6
        ++pos;
        auto pos1 = hostport.find(']', pos);
        if(pos1 == std::string::npos) {
            return false;
        }
        host.assign(hostport.begin()+pos, hostport.begin()+pos1);
        pos = pos1 + 1;
        pos = hostport.find(':', pos);
        if(pos != std::string::npos) {
            port.assign(hostport.begin()+pos+1, hostport.end());
        }
    } else {
        pos = hostport.find(':');
        if(pos != std::string::npos) {
            host.assign(hostport.begin(), hostport.begin()+pos);
            port.assign(hostport.begin()+pos+1, hostport.end());
        } else {
            host = hostport;
        }
    }
    return true;
}
