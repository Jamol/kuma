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

#include "ProxyAuthenticator.h"
#include "util/util.h"
#include "util/kmtrace.h"

#if defined(KUMA_OS_WIN)
# include <windows.h>
# include "SspiAuthenticator.h"
#elif defined(KUMA_OS_MAC)
# include "GssapiAuthenticator.h"
#elif defined(KUMA_OS_LINUX)

#else
# error "UNSUPPORTED OS"
#endif

#include "BasicAuthenticator.h"

using namespace kuma;

ProxyAuthenticator::ProxyAuthenticator()
{
    
}

ProxyAuthenticator::~ProxyAuthenticator()
{
    
}

ProxyAuthenticator::AuthScheme ProxyAuthenticator::getAuthScheme(const std::string &scheme)
{
    if (is_equal(scheme, "Basic")) {
        return AuthScheme::BASIC;
    } else if (is_equal(scheme, "NTLM")) {
        return AuthScheme::NTLM;
    }
    else if (is_equal(scheme, "Digest")) {
        return AuthScheme::DIGEST;
    }
    else if (is_equal(scheme, "Negotiate")) {
        return AuthScheme::NEGOTIATE;
    }
    else {
        return AuthScheme::UNKNOWN;
    }
}

std::string ProxyAuthenticator::getAuthScheme(AuthScheme scheme)
{
    switch (scheme)
    {
    case AuthScheme::BASIC:
        return "Basic";
    case AuthScheme::NTLM:
        return "NTLM";
    case AuthScheme::DIGEST:
        return "Digest";
    case AuthScheme::NEGOTIATE:
        return "Negotiate";
    default:
        return "";
    }
}

ProxyAuthenticator::Ptr ProxyAuthenticator::create(const std::string &scheme, const AuthInfo &auth_info, const RequestInfo &req_info)
{
    switch (auth_info.scheme) {
        case AuthScheme::BASIC:
        {
            auto *basic = new BasicAuthenticator();
            auto ptr =  Ptr(basic);
            if (!basic->init(auth_info.user, auth_info.passwd)) {
                ptr.reset();
            }
            return ptr;
        }
            
        case AuthScheme::NTLM:
        case AuthScheme::NEGOTIATE:
#if defined(KUMA_OS_MAC)
        {
            auto *gssapi = new GssapiAuthenticator();
            auto ptr =  Ptr(gssapi);
            if (!gssapi->init(auth_info, req_info)) {
                ptr.reset();
            }
            return ptr;
        }
#endif
        case AuthScheme::DIGEST:
#if defined(KUMA_OS_WIN)
        {
            auto *sspi = new SspiAuthenticator();
            auto ptr =  Ptr(sspi);
            if (!sspi->init(auth_info, req_info)) {
                ptr.reset();
            }
            return ptr;
        }
#endif
            
        default:
            KUMA_ERRTRACE("ProxyAuthenticator::create, unsupported auth scheme: " << scheme);
            return Ptr();
    }
}
