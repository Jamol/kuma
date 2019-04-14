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

#ifndef __Http2Response_H__
#define __Http2Response_H__

#include "kmdefs.h"
#include "H2ConnectionImpl.h"
#include "http/HttpResponseImpl.h"
#include "http/HttpHeader.h"

KUMA_NS_BEGIN

class H2StreamProxy;

class Http2Response : public HttpResponse::Impl, public DestroyDetector
{
public:
    Http2Response(const EventLoopPtr &loop, std::string ver);
    
    KMError attachStream(uint32_t stream_id, H2Connection::Impl* conn) override;
    KMError addHeader(std::string name, std::string value) override;
    KMError sendResponse(int status_code, const std::string& desc, const std::string& ver) override;
    int sendBody(const void* data, size_t len) override;
    int sendBody(const KMBuffer &buf) override;
    KMError close() override;
    
    bool isHttp2() const override { return true; }
    
    const std::string& getMethod() const override;
    const std::string& getPath() const override;
    const std::string& getQuery() const override { return EmptyString; }
    const std::string& getVersion() const override { return VersionHTTP2_0; }
    const std::string& getParamValue(const std::string &name) const override;
    const std::string& getHeaderValue(const std::string &name) const override;
    void forEachHeader(const EnumerateCallback &cb) const override;
    
protected:
    void onHeader();
    void onData(KMBuffer &buf);
    void onWrite();
    void onError(KMError err);
    void onComplete();
    
private:
    bool canSendBody() const override;
    void checkResponseHeaders() override;
    void checkRequestHeaders() override;
    HttpHeader& getRequestHeader() override;
    const HttpHeader& getRequestHeader() const override;
    HttpHeader& getResponseHeader() override;
    const HttpHeader& getResponseHeader() const override;
    
private:
    std::unique_ptr<H2StreamProxy> stream_;
};

KUMA_NS_END

#endif /* __H2Response_H__ */
