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

#include "BasicAuthenticator.h"
#include "util/kmtrace.h"
#include "util/base64.h"


using namespace kuma;

BasicAuthenticator::BasicAuthenticator()
{
    
}

BasicAuthenticator::~BasicAuthenticator()
{
    
}

bool BasicAuthenticator::init(const std::string &user, const std::string &passwd)
{
    if (!user.empty()) {
        std::string str = user + ":" + passwd;
        auth_token_ = x64_encode(str, false);
    }
    return true;
}

bool BasicAuthenticator::nextAuthToken(const std::string& challenge)
{
    if (!hasAuthHeader())
    {
        return false;
    }
    
    return true;
}

std::string BasicAuthenticator::getAuthHeader() const
{
    if (hasAuthHeader()) {
        return "Basic " + auth_token_;
    }
    
    return "";
}

bool BasicAuthenticator::hasAuthHeader() const
{
    return !auth_token_.empty();
}
