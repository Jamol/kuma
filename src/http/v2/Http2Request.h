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
#include "util/kmobject.h"
#include "util/DestroyDetector.h"

KUMA_NS_BEGIN

class Http2Request : public KMObject, public DestroyDetector, public HttpRequest::Impl
{
public:
    Http2Request(EventLoop::Impl* loop, std::string ver);
    ~Http2Request();
    
    KMError setSslFlags(uint32_t ssl_flags) override;
    void addHeader(std::string name, std::string value) override;
    int sendData(const void* data, size_t len) override;
    KMError close() override;
    
    int getStatusCode() const override { return status_code_; }
    const std::string& getVersion() const override { return VersionHTTP2_0; }
    const std::string& getHeaderValue(std::string name) const override;
    void forEachHeader(EnumrateCallback cb) override;
    
public:
    void onHeaders(const HeaderVector &headers, bool endHeaders, bool endSteam);
    void onData(void *data, size_t len, bool endSteam);
    void onRSTStream(int err);
    void onWrite();
    
private:
    void onConnect(KMError err);
    void onError(KMError err);
    
    KMError sendRequest() override;
    void checkHeaders() override;
    
    //{ in H2Connection thread
    size_t buildHeaders(HeaderVector &headers);
    KMError sendRequest_i();
    KMError sendHeaders();
    int sendData_i(const void* data, size_t len, bool newData=false);
    void close_i();
    //}
    
private:
    EventLoop::Impl* loop_;
    H2ConnectionPtr conn_;
    H2StreamPtr stream_;
    
    // request
    HeaderMap req_headers;
    bool has_content_length_ = false;
    bool is_chunked_ = false;
    size_t content_length_ = 0;
    size_t body_bytes_sent_ = 0;
    
    // response
    int status_code_ = 0;
    HeaderMap rsp_headers_;
    
    bool write_blocked_ { false };
    std::list<iovec> data_list_;
    
    std::string connKey_;
};

KUMA_NS_END

#endif /* __H2Request_H__ */
