/* Copyright (c) 2016, Fengping Bao <jamol@live.com>
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

#ifndef __H2Request_H__
#define __H2Request_H__

#include "kmdefs.h"
#include "H2ConnectionImpl.h"
#include "http/HttpRequestBase.h"
#include "util/kmobject.h"
#include "util/DestroyDetector.h"

KUMA_NS_BEGIN

class H2Request : public KMObject, public DestroyDetector, public HttpRequestBase
{
public:
    H2Request(EventLoopImpl* loop);
    ~H2Request();
    
    KMError setSslFlags(uint32_t ssl_flags) override;
    int sendData(const uint8_t* data, size_t len) override;
    KMError close() override;
    
    int getStatusCode() const override { return status_code_; }
    const std::string& getVersion() const override { return VersionHTTP2_0; }
    const std::string& getHeaderValue(std::string name) const override;
    void forEachHeader(EnumrateCallback cb) override;
    
public:
    void onHeaders(const HeaderVector &headers, bool endSteam);
    void onData(uint8_t *data, size_t len, bool endSteam);
    void onRSTStream(int err);
    
private:
    void onConnect(KMError err);
    
    KMError sendRequest() override;
    void checkHeaders() override;
    size_t buildHeaders(HeaderVector &headers);
    void sendHeaders();
    
private:
    EventLoopImpl* loop_;
    H2ConnectionPtr conn_ = nullptr;
    H2StreamPtr stream_ = nullptr;
    
    int status_code_ = 0;
    HeaderMap rsp_headers_;
    
    std::string connKey_;
};

KUMA_NS_END

#endif /* __H2Request_H__ */
