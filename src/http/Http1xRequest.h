/* Copyright (c) 2014, Fengping Bao <jamol@live.com>
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

#ifndef __Http1xRequest_H__
#define __Http1xRequest_H__

#include "kmdefs.h"
#include "TcpConnection.h"
#include "Uri.h"
#include "HttpRequestImpl.h"
#include "H1xStream.h"
#include "libkev/src/utils/DestroyDetector.h"

KUMA_NS_BEGIN

class Http1xRequest : public kev::DestroyDetector, public HttpRequest::Impl
{
public:
    Http1xRequest(const EventLoopPtr &loop, std::string ver);
    ~Http1xRequest();
    
    KMError setSslFlags(uint32_t ssl_flags) override { return stream_->setSslFlags(ssl_flags); }
    KMError setProxyInfo(const ProxyInfo &proxy_info) override;
    KMError addHeader(std::string name, std::string value) override;
    int sendBody(const void* data, size_t len) override;
    int sendBody(const KMBuffer &buf) override;
    void reset() override; // reset for connection reuse
    KMError close() override;
    
    int getStatusCode() const override;
    const std::string& getVersion() const override { return stream_->getVersion(); }
    
protected:
    void onWrite();
    void onError(KMError err);

private:
    KMError sendRequest() override;
    bool canSendBody() const override;
    HttpHeader& getRequestHeader() override;
    const HttpHeader& getRequestHeader() const override;
    HttpHeader& getResponseHeader() override;
    const HttpHeader& getResponseHeader() const override;
    void cleanup();
    bool isVersion2() override { return false; }
    bool processHttpCache();
    
    void onCacheComplete();
    void onRequestComplete();
    
private:
    std::unique_ptr<H1xStream> stream_;
    
    int                     rsp_cache_status_ = 0;
    KMBuffer::Ptr           rsp_cache_body_;
};

KUMA_NS_END

#endif
