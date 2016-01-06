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

KUMA_NS_BEGIN

class HttpRequestImpl
{
public:
    typedef std::function<void(uint8_t*, uint32_t)> DataCallback;
    typedef std::function<void(int)> EventCallback;
    typedef std::function<void(void)> HttpEventCallback;
    
    HttpRequestImpl(EventLoopImpl* loop);
    ~HttpRequestImpl();
    
    void addHeader(const std::string& name, const std::string& value);
    void addHeader(const std::string& name, uint32_t value);
    int sendRequest(const std::string& method, const std::string& url, const std::string& ver = "HTTP/1.1");
    int sendData(const uint8_t* data, uint32_t len);
    void reset(); // reset for connection reuse
    int close();
    
    int getStatusCode() { return http_parser_.getStatusCode(); }
    const std::string& getVersion() { return http_parser_.getVersion(); }
    const std::string& getHeaderValue(const char* name) { return http_parser_.getHeaderValue(name); }
    void forEachHeader(HttpParserImpl::EnumrateCallback& cb) { return http_parser_.forEachHeader(cb); }
    void forEachHeader(HttpParserImpl::EnumrateCallback&& cb) { return http_parser_.forEachHeader(cb); }
    
    void setDataCallback(DataCallback& cb) { cb_data_ = cb; }
    void setWriteCallback(EventCallback& cb) { cb_write_ = cb; }
    void setErrorCallback(EventCallback& cb) { cb_error_ = cb; }
    void setHeaderCompleteCallback(HttpEventCallback& cb) { cb_header_ = cb; }
    void setResponseCompleteCallback(HttpEventCallback& cb) { cb_response_ = cb; }
    void setDataCallback(DataCallback&& cb) { cb_data_ = std::move(cb); }
    void setWriteCallback(EventCallback&& cb) { cb_write_ = std::move(cb); }
    void setErrorCallback(EventCallback&& cb) { cb_error_ = std::move(cb); }
    void setHeaderCompleteCallback(HttpEventCallback&& cb) { cb_header_ = std::move(cb); }
    void setResponseCompleteCallback(HttpEventCallback&& cb) { cb_response_ = std::move(cb); }
    
protected: // callbacks of tcp_socket
    void onConnect(int err);
    void onSend(int err);
    void onReceive(int err);
    void onClose(int err);
    
protected:
    const char* getObjKey();

private:
    enum State {
        STATE_IDLE,
        STATE_CONNECTING,
        STATE_SENDING_HEADER,
        STATE_SENDING_BODY,
        STATE_RECVING_RESPONSE,
        STATE_COMPLETE,
        STATE_WAIT_FOR_REUSE,
        STATE_ERROR,
        STATE_CLOSED
    };
    void setState(State state) { state_ = state; }
    State getState() { return state_; }
    void buildRequest();
    int sendChunk(const uint8_t* data, uint32_t len);
    void cleanup();
    void sendRequestHeader();
    
    void onHttpData(const char* data, uint32_t len);
    void onHttpEvent(HttpEvent ev);
    
private:
    HttpParserImpl          http_parser_;
    State                   state_;
    
    std::vector<uint8_t>    send_buffer_;
    uint32_t                send_offset_;
    TcpSocketImpl           tcp_socket_;
    
    HttpParserImpl::STRING_MAP  header_map_;
    std::string             method_;
    std::string             url_;
    std::string             version_;
    Uri                     uri_;
    
    bool                    is_chunked_;
    bool                    has_content_length_;
    uint32_t                content_length_;
    uint32_t                body_bytes_sent_;
    
    DataCallback            cb_data_;
    EventCallback           cb_write_;
    EventCallback           cb_error_;
    HttpEventCallback       cb_header_;
    HttpEventCallback       cb_response_;
    
    bool*                   destroy_flag_ptr_;
};

KUMA_NS_END

#endif
