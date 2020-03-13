/* Copyright (c) 2016-2019, Fengping Bao <jamol@live.com>
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

#pragma once

#include "kmdefs.h"
#include "H2ConnectionImpl.h"
#include "http/HttpHeader.h"
#include "http/Uri.h"
#include "libkev/src/util/kmobject.h"
#include "libkev/src/util/DestroyDetector.h"
#include "libkev/src/util/kmqueue.h"
#include "proxy/proxydefs.h"

KUMA_NS_BEGIN


class H2StreamProxy : public kev::KMObject, public kev::DestroyDetector
{
public:
    using DataCallback = std::function<void(KMBuffer &buf)>;
    using EventCallback = HttpRequest::EventCallback;
    using HeaderCallback = HttpRequest::HttpEventCallback;
    using CompleteCallback = HttpRequest::HttpEventCallback;
    
    H2StreamProxy(const EventLoopPtr &loop);
    ~H2StreamProxy();
    
    KMError setProxyInfo(const ProxyInfo &proxy_info);
    KMError addHeader(std::string name, std::string value);
    KMError sendRequest(std::string method, std::string url, uint32_t ssl_flags);
    KMError attachStream(uint32_t stream_id, H2Connection::Impl* conn);
    KMError sendResponse(int status_code);
    int sendData(const void* data, size_t len);
    int sendData(const KMBuffer &buf);
    void reset();
    KMError close();
    
    bool isServer() const { return is_server_; }
    bool canSendData() const;
    
    HttpHeader& getOutgoingHeaders() { return outgoing_header_; }
    HttpHeader& getIncomingHeaders() { return incoming_header_; }
    const std::string& getMethod() const { return method_; }
    const std::string& getPath() const { return path_; }
    int getStatusCode() const { return status_code_; }
    EventLoopPtr eventLoop() const { return loop_.lock(); }
    
    template<typename Runnable> // (void)
    bool runOnLoopThread(Runnable &&r, bool maybe_sync=true)
    {
        if (maybe_sync && is_same_loop_) {
            r();
            return true;
        } else if (auto loop = loop_.lock()) {
            return loop->post([r = std::move(r)]{
                r();
            }, &loop_token_) == kev::Result::OK;
        } else {
            r();
            return true;
        }
    }
    
    void setHeaderCallback(HeaderCallback cb) { header_cb_ = std::move(cb); }
    void setDataCallback(DataCallback cb) { data_cb_ = std::move(cb); }
    void setWriteCallback(EventCallback cb) { write_cb_ = std::move(cb); }
    void setErrorCallback(EventCallback cb) { error_cb_ = std::move(cb); }
    void setIncomingCompleteCallback(CompleteCallback cb) { incoming_complete_cb_ = std::move(cb); }
    void setOutgoingCompleteCallback(CompleteCallback cb) { outgoing_complete_cb_ = std::move(cb); }
    
protected:
    //{ on conn_ thread
    void onConnect_i(KMError err);
    void onError_i(KMError err);
    void onHeaders_i(const HeaderVector &headers, bool end_stream);
    void onData_i(KMBuffer &buf, bool end_stream);
    void onRSTStream_i(int err);
    void onWrite_i();
    void onOutgoingComplete_i();
    void onIncomingComplete_i();
    //}
    
    void setupStreamCallbacks();
    
    void saveRequestData(const void *data, size_t len);
    void saveRequestData(const KMBuffer &buf);
    void saveResponseData(const void *data, size_t len);
    void saveResponseData(const KMBuffer &buf);
    
    //{ on conn_ thread
    size_t buildRequestHeaders(HeaderVector &headers);
    size_t buildResponseHeaders(HeaderVector &headers);
    KMError sendRequest_i();
    KMError sendResponse_i();
    KMError sendHeaders_i();
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
    void onHeaders(bool end_stream);
    void onData(bool end_stream);
    void onPushPromise(bool end_stream);
    void onWrite();
    void onError(KMError err);
    void checkResponseStatus(bool end_stream);
    
    void onHeaderComplete();
    void onStreamData(KMBuffer &buf);    
    void onOutgoingComplete();
    void onIncomingComplete();
    //}
    
    std::string getCacheKey()
    {
        std::string cache_key = uri_.getHost() + uri_.getPath();
        if (!uri_.getQuery().empty()) {
            cache_key += "?";
            cache_key += uri_.getQuery();
        }
        return cache_key;
    }
    
    template<typename Runnable> // (void)
    bool runOnStreamThread(Runnable &&r)
    {
        if (is_same_loop_) {
            r();
            return true;
        } else if (auto loop = conn_loop_.lock()) {
            return loop->post([r = std::move(r)] {
                r();
            }, &conn_token_) == kev::Result::OK;
        } else {
            r();
            return true;
        }
    }
    
    enum State {
        IDLE,
        CONNECTING,
        OPEN,
        IN_ERROR,
        CLOSED
    };
    void setState(State state) { state_ = state; }
    State getState() const { return state_; }
    
protected:
    State state_ = State::IDLE;
    EventLoopWeakPtr loop_;
    EventLoopWeakPtr conn_loop_;
    H2ConnectionPtr conn_;
    H2StreamPtr stream_;
    bool is_server_ = false;
    bool is_same_loop_ = false;
    
    std::string method_;
    std::string path_;
    int status_code_ = 0;
    
    // outcoming
    size_t body_bytes_sent_ = 0;
    uint32_t ssl_flags_ = 0;
    Uri uri_;
    std::string protocol_;
    HttpHeader outgoing_header_{true, true};
    ProxyInfo proxy_info_;
    
    // incoming
    HttpHeader incoming_header_{false, true};
    kev::KMQueue<KMBuffer::Ptr> recv_buf_queue_;
    bool header_complete_ {false};
    
    bool write_blocked_ { false };
    kev::KMQueue<KMBuffer::Ptr>  send_buf_queue_;
    
    HeaderCallback          header_cb_;
    DataCallback            data_cb_;
    EventCallback           write_cb_;
    EventCallback           error_cb_;
    CompleteCallback        incoming_complete_cb_;
    CompleteCallback        outgoing_complete_cb_;
    
    EventLoopToken          loop_token_;
    EventLoopToken          conn_token_;
};

KUMA_NS_END

