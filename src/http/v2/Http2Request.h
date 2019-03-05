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
#include "util/kmqueue.h"

KUMA_NS_BEGIN

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
    KMError addHeader(std::string name, std::string value) override;
    int sendBody(const void* data, size_t len) override;
    int sendBody(const KMBuffer &buf) override;
    void reset() override; // reset for connection reuse
    KMError close() override;
    
    int getStatusCode() const override { return status_code_; }
    const std::string& getVersion() const override { return VersionHTTP2_0; }
    const std::string& getHeaderValue(const std::string &name) const override;
    void forEachHeader(const EnumerateCallback &cb) const override;
    
protected:
    //{ on conn_ thread
    void onConnect(KMError err);
    void onError(KMError err);
    void onHeaders(const HeaderVector &headers, bool end_stream);
    void onData(KMBuffer &buf, bool end_stream);
    void onRSTStream(int err);
    void onWrite();
    //}
    
    KMError sendRequest() override;
    bool canSendBody() const override;
    void checkResponseHeaders() override;
    void checkRequestHeaders() override;
    HttpHeader& getRequestHeader() override;
    const HttpHeader& getResponseHeader() const override;
    void setupStreamCallbacks();
    
    /*
     * check if HTTP cache is available
     */
    bool processHttpCache(const EventLoopPtr &loop);
    void saveRequestData(const void *data, size_t len);
    void saveRequestData(const KMBuffer &buf);
    void saveResponseData(const void *data, size_t len);
    void saveResponseData(const KMBuffer &buf);
    
    //{ on conn_ thread
    size_t buildHeaders(HeaderVector &headers);
    KMError sendRequest_i();
    KMError sendHeaders();
    int sendData_i(const void* data, size_t len);
    int sendData_i(const KMBuffer &buf);
    int sendData_i();
    void close_i();
    
    /*
     * check if server push is available
     */
    bool processPushPromise();
    //}
    
    //{ on loop_ thread
    void onHeaders();
    void onData();
    void onCacheComplete();
    void onPushPromise();
    void onWrite_i();
    void onError_i(KMError err);
    void checkResponseStatus();
    //}
    
protected:
    EventLoopWeakPtr loop_;
    H2ConnectionPtr conn_;
    H2StreamPtr stream_;
    
    // request
    size_t body_bytes_sent_ = 0;
    uint32_t ssl_flags_ = 0;
    HttpHeader req_header_{true};
    
    // response
    int status_code_ = 0;
    HttpHeader rsp_header_{false};
    KMQueue<KMBuffer::Ptr> rsp_queue_;
    bool header_complete_ = false;
    bool response_complete_ = false;
    
    bool closing_ = { false };
    bool write_blocked_ { false };
    KMQueue<KMBuffer::Ptr> req_queue_;
    
    EventLoopToken loop_token_;
    EventLoopToken conn_token_;
};

KUMA_NS_END

#endif /* __H2Request_H__ */
