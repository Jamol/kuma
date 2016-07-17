/* Copyright (c) 2014, Fengping Bao <jamol@live.com>
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef __HttpRequestImpl_H__
#define __HttpRequestImpl_H__

#include "kmdefs.h"
#include "HttpParserImpl.h"
#include "TcpSocketImpl.h"
#include "Uri.h"
#include "IHttpRequest.h"
#include "util/kmobject.h"

KUMA_NS_BEGIN

class HttpRequestImpl : public KMObject, public IHttpRequest
{
public:
    HttpRequestImpl(EventLoopImpl* loop);
    ~HttpRequestImpl();
    
    int setSslFlags(uint32_t ssl_flags);
    int sendData(const uint8_t* data, size_t len);
    void reset(); // reset for connection reuse
    int close();
    
    int getStatusCode() const { return http_parser_.getStatusCode(); }
    const std::string& getVersion() const { return http_parser_.getVersion(); }
    const std::string& getHeaderValue(const char* name) const { return http_parser_.getHeaderValue(name); }
    void forEachHeader(HttpParserImpl::EnumrateCallback cb) { return http_parser_.forEachHeader(std::move(cb)); }
    
protected: // callbacks of tcp_socket
    void onConnect(int err);
    void onSend(int err);
    void onReceive(int err);
    void onClose(int err);

private:
    int sendRequest();
    void checkHeaders();
    void buildRequest();
    int sendChunk(const uint8_t* data, size_t len);
    void cleanup();
    void sendRequestHeader();
    bool isVersion1_1() { return true; }
    
    void onHttpData(const char* data, size_t len);
    void onHttpEvent(HttpEvent ev);
    
private:
    HttpParserImpl          http_parser_;
    
    std::vector<uint8_t>    send_buffer_;
    uint32_t                send_offset_ = 0;
    TcpSocketImpl           tcp_socket_;
    
    bool*                   destroy_flag_ptr_ = nullptr;
};

KUMA_NS_END

#endif
