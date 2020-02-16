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

#ifndef __Http2Request_H__
#define __Http2Request_H__

#include "kmdefs.h"
#include "H2ConnectionImpl.h"
#include "http/HttpRequestImpl.h"
#include "http/HttpHeader.h"
#include "util/kmobject.h"
#include "util/DestroyDetector.h"

KUMA_NS_BEGIN

class H2StreamProxy;

/**
 * this class implement HTTP2 request
 *
 * the first Http2Request is responsible for create H2Connection, and H2Connection run on same EventLoop as
 * Http2Request. other Http2Requests to same host will reuse this H2Connection. if Http2Request EventLoop 
 * is different with H2Connection's, thread switch and data copy(if any) occur.
 * Http2Request will check HTTP cache firstly, and then check if there is push promise for this request. 
 * if none of them hit, a HTTP2 stream will be launched to complete the request.
 */
class Http2Request : public DestroyDetector, public HttpRequest::Impl
{
public:
    Http2Request(const EventLoopPtr &loop, std::string ver);
    ~Http2Request();
    
    KMError setSslFlags(uint32_t ssl_flags) override;
    KMError setProxyInfo(const ProxyInfo &proxy_info) override;
    KMError addHeader(std::string name, std::string value) override;
    int sendBody(const void* data, size_t len) override;
    int sendBody(const KMBuffer &buf) override;
    void reset() override; // reset for connection reuse
    KMError close() override;
    
    bool isHttp2() const override { return true; }
    
    int getStatusCode() const override;
    const std::string& getVersion() const override { return VersionHTTP2_0; }
    
protected:
    KMError sendRequest() override;
    bool canSendBody() const override;
    void checkResponseHeaders() override;
    HttpHeader& getRequestHeader() override;
    const HttpHeader& getRequestHeader() const override;
    HttpHeader& getResponseHeader() override;
    const HttpHeader& getResponseHeader() const override;
    
    bool processHttpCache();
    void onCacheComplete();
    
    void onHeader();
    void onData(KMBuffer &buf);
    void onWrite();
    void onError(KMError err);
    void onRequestComplete();
    void onComplete();
    
protected:
    std::unique_ptr<H2StreamProxy> stream_;
    
    uint32_t                ssl_flags_ = 0;
    int                     rsp_cache_status_ = 0;
    KMBuffer::Ptr           rsp_cache_body_;
};

KUMA_NS_END

#endif /* __H2Request_H__ */
