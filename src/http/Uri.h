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

#ifndef __Uri_H__
#define __Uri_H__

#include "kmdefs.h"
#include <string>
#include <map>
#include <vector>

KUMA_NS_BEGIN

class Uri
{
public:
	Uri();
    Uri(const std::string& url);
	~Uri();

    bool parse(const std::string& url);
    
    const std::string& getScheme() const { return scheme_; }
    const std::string& getHost() const { return host_; }
    const std::string& getPort() const { return port_; }
    const std::string& getPath() const { return path_; }
    const std::string& getQuery() const { return query_; }
    const std::string& getFragment() const { return fragment_; }
    
private:
    bool parse_host_port(const std::string& url, std::string::size_type &pos, std::string& host, std::string& port);

private:
    std::string         scheme_;
    std::string         host_;
    std::string         port_;
	std::string         path_;
    std::string         query_;
    std::string         fragment_;
};

KUMA_NS_END

#endif
