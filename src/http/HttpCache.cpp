/* Copyright © 2014-2017, Fengping Bao <jamol@live.com>
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

#include "HttpCache.h"
#include "util/kmtrace.h"

KUMA_NS_USING

bool HttpCache::getCache(const std::string &key, int &status_code, HeaderVector &headers, HttpBody &body)
{
    std::lock_guard<std::mutex> g(mutex_);
    auto it = caches_.find(key);
    if (it == caches_.end()) {
        return false;
    }
    auto now_time = steady_clock::now();
    if (now_time > it->second.expire_time) {
        caches_.erase(it);
        return false;
    }
    status_code = it->second.status_code;
    auto age = duration_cast<std::chrono::seconds>(now_time - it->second.receive_time).count();
    headers = it->second.headers;
    headers.emplace_back("Age", std::to_string(age));
    body = it->second.body;
    return true;
}

void HttpCache::setCache(const std::string &key, int status_code, HeaderVector headers, const uint8_t *body, size_t body_size)
{
    HttpBody b;
    if (body && body_size > 0) {
        b.assign(body, body + body_size);
    }
    setCache(key, status_code, std::move(headers), std::move(b));
}

void HttpCache::setCache(const std::string &key, int status_code, HeaderVector headers, HttpBody body)
{
    auto max_age = getMaxAgeOfCache(headers);
    KUMA_INFOTRACE("HttpCache::setCache, key="<<key<<", max_age="<<max_age<<", body="<<body.size());
    if (max_age <= 0) {
        return;
    }
    std::lock_guard<std::mutex> g(mutex_);
    caches_[key] = {status_code, std::move(headers), std::move(body), max_age};
}

HttpCache& HttpCache::get()
{
    static HttpCache inst;
    return inst;
}

bool HttpCache::isCacheable(const HeaderVector &headers)
{
    bool cacheable = true;
    for (auto &kv : headers) {
        if (is_equal(kv.first, strCacheControl)) {
            auto &directives = kv.second;
            for_each_token(directives, ',', [&cacheable] (std::string &d) {
                if (is_equal(d, "no-store") || is_equal(d, "no-cache")) {
                    cacheable = false;
                    return false;
                }
                return true;
            });
        }
    }
    return cacheable;
}

int HttpCache::getMaxAgeOfCache(const HeaderVector &headers)
{
    int max_age = 0;
    for (auto &kv : headers) {
        if (is_equal(kv.first, strCacheControl)) {
            auto &directives = kv.second;
            for_each_token(directives, ',', [&max_age] (std::string &d) {
                if (is_equal(d, "no-store") || is_equal(d, "no-cache")) {
                    return false;
                }
                auto p = d.find("max-age=");
                if (p != std::string::npos) {
                    auto s = d.substr(p + 8);
                    max_age = std::stoi(s);
                    return false;
                }
                return true;
            });
        }
    }
    return max_age;
}
