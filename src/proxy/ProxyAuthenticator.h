/* Copyright (c) 2014-2019, Fengping Bao <jamol@live.com>
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

#pragma once

#include "kmdefs.h"

#include <string>
#include <memory>

KUMA_NS_BEGIN


class ProxyAuthenticator
{
public:
    using Ptr = std::unique_ptr<ProxyAuthenticator>;
    enum class AuthScheme
    {
        UNKNOWN,
        BASIC,
        NTLM,
        DIGEST,
        NEGOTIATE
    };
    struct AuthInfo
    {
        AuthScheme scheme;
        std::string user;
        std::string passwd;
    };
    struct RequestInfo
    {
        std::string host;
        uint16_t port;
        std::string method;
        std::string path;
        std::string service;
    };

    ProxyAuthenticator();
    virtual ~ProxyAuthenticator();
    
    virtual bool nextAuthToken(const std::string& challenge) = 0;
    virtual std::string getAuthHeader() const = 0;
    virtual bool hasAuthHeader() const = 0;

    static AuthScheme getAuthScheme(const std::string &scheme);
    static std::string getAuthScheme(AuthScheme scheme);
    
    static Ptr create(const std::string &scheme, const AuthInfo &auth_info, const RequestInfo &req_info);
};

KUMA_NS_END
