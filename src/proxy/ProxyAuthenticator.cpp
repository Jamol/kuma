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
# include "GssAuthenticator.h"
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

ProxyAuthenticator::Ptr ProxyAuthenticator::create(const std::string scheme,
                                                   const std::string &domain_user,
                                                   const std::string &passwd)
{
    if (is_equal(scheme, "Basic")) {
        auto *basic = new BasicAuthenticator();
        auto ptr =  Ptr(basic);
        if (!basic->init(domain_user, passwd)) {
            ptr.reset();
        }
        return ptr;
    } else if (is_equal(scheme, "NTLM") || is_equal(scheme, "Negotiate")) {
#if defined(KUMA_OS_WIN)
        auto *sspi = new SspiAuthenticator();
        auto ptr =  Ptr(sspi);
        if (!sspi->init("", scheme, domain_user, passwd)) {
            ptr.reset();
        }
        return ptr;
#elif defined(KUMA_OS_MAC)
        auto *gss = new GssAuthenticator();
        auto ptr =  Ptr(gss);
        if (!gss->init("", scheme, domain_user, passwd)) {
            ptr.reset();
        }
        return ptr;
#else
#endif
    }
    
    return Ptr();
}
