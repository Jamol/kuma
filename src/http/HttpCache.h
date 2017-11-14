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

#ifndef __HttpCache_H__
#define __HttpCache_H__

#include "httpdefs.h"
#include "kmbuffer.h"

#include <memory>
#include <map>
#include <vector>
#include <chrono>
#include <mutex>

using namespace std::chrono;

KUMA_NS_BEGIN

class HttpCache
{
public:
    bool getCache(const std::string &key, int &status_code, HeaderVector &headers, KMBuffer &body);
    //void setCache(const std::string &key, int status_code, HeaderVector headers, const uint8_t *body, size_t body_size);
    void setCache(const std::string &key, int status_code, HeaderVector headers, KMBuffer &body);
    
    static HttpCache& instance();
    static bool isCacheable(const std::string &method, const HeaderVector &headers);
    static int getMaxAgeOfCache(const HeaderVector &headers);
    
protected:
    HttpCache() {}
    
protected:
    class CacheRecord
    {
    public:
        CacheRecord() = default;
        CacheRecord(int code, HeaderVector &&h, KMBuffer &b, int max_age)
            : status_code(code), headers(std::move(h)), body(b.clone()), max_age(max_age)
        {
            receive_time = steady_clock::now();
            expire_time = receive_time + seconds(max_age);
        }
        CacheRecord(CacheRecord &&other)
        {
            headers = std::move(other.headers);
            body = std::move(other.body);
            receive_time = std::move(other.receive_time);
            expire_time = std::move(other.expire_time);
        }
        CacheRecord& operator=(CacheRecord &&other)
        {
            if (this != &other) {
                headers = std::move(other.headers);
                body = std::move(other.body);
                receive_time = std::move(other.receive_time);
                expire_time = std::move(other.expire_time);
            }
            return *this;
        }
        int status_code = 0;
        HeaderVector headers;
        KMBuffer::Ptr body;
        int max_age = 0;
        time_point<steady_clock> receive_time;
        time_point<steady_clock> expire_time;
    };
    using CacheMap = std::map<std::string, CacheRecord>;
    
    CacheMap caches_;
    std::mutex mutex_;
};

KUMA_NS_END

#endif /* __HttpCache_H__ */
