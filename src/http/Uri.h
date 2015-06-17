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
    
    const std::string& getScheme() { return scheme_; }
    const std::string& getHost() { return host_; }
    const std::string& getPort() { return port_; }
    const std::string& getPath() { return path_; }
    const std::string& getQuery() { return query_; }
    const std::string& getFragment() { return fragment_; }
    
private:
    bool parse_host_port(const std::string& hostport, std::string& host, std::string& port);

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
