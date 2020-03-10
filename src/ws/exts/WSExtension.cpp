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

#include "WSExtension.h"
#include "libkev/src/util/util.h"

using namespace kuma;
using namespace kuma::ws;


KMError WSExtension::parseKeyValue(const std::string &str, std::string &key, std::string &value)
{
    auto pos = str.find('=');
    if (pos != std::string::npos) {
        key = str.substr(0, pos);
        value = str.substr(pos + 1);
        kev::trim_left(key, ' ');
        kev::trim_right(key, ' ');
        kev::trim_left(value, ' ');
        kev::trim_right(value, ' ');
        if (!value.empty() && *value.begin() == '\"') {
            value.erase(value.begin());
        }
        if (!value.empty() && *value.rbegin() == '\"') {
            value.pop_back();
        }
    } else {
        key = str;
    }
    
    return !key.empty() ? KMError::NOERR : KMError::INVALID_PARAM;
}

KMError WSExtension::parseParameterList(const std::string &parameters, KeyValueList &param_list)
{
    kev::for_each_token(parameters, ';', [&param_list] (std::string &str) {
        std::string key, value;
        auto ret = parseKeyValue(str, key, value);
        if (ret == KMError::NOERR) {
            param_list.emplace_back(key, value);
        }
        
        return true;
    });
    
    return !param_list.empty() ? KMError::NOERR : KMError::INVALID_PARAM;
}
