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
#include "proxy/ProxyAuthenticator.h"
#include "libkev/src/util/kmobject.h"

#include <string>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

#undef SECURITY_WIN32
#undef SECURITY_KERNEL
#define SECURITY_WIN32 1
#include <sspi.h>

KUMA_NS_BEGIN

class SspiAuthenticator : public KMObject, public ProxyAuthenticator
{
public:
    SspiAuthenticator();
    ~SspiAuthenticator();

    bool init(const AuthInfo &auth_info, const RequestInfo &req_info);
    bool nextAuthToken(const std::string& challenge) override;
    std::string getAuthHeader() const override;
    bool hasAuthHeader() const override;

protected:
    bool parseDigestChallenge(
        const std::string &challenge, 
        std::string &realm, 
        std::string &nonce, 
        std::string qop);
    void cleanup();

protected:
    AuthScheme      auth_scheme_;
    RequestInfo     req_info_;
    std::string     auth_token_;
    CredHandle      cred_handle_{ 0 };
    CtxtHandle      ctxt_handle_{ 0 };
};

KUMA_NS_END
