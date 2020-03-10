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
#include "EventLoopImpl.h"
#include "TcpConnection.h"
#include "http/HttpHeader.h"
#include "http/HttpMessage.h"
#include "http/HttpParserImpl.h"
#include "http/Uri.h"
#include "libkev/src/util/kmobject.h"
#include "libkev/src/util/DestroyDetector.h"
#include "proxy/ProxyConnectionImpl.h"

KUMA_NS_BEGIN


class H1xStream : public kev::KMObject, public kev::DestroyDetector
{
public:
    using DataCallback = std::function<void(KMBuffer &)>;
    using EventCallback = HttpRequest::EventCallback;
    using HeaderCallback = HttpRequest::HttpEventCallback;
    using CompleteCallback = HttpRequest::HttpEventCallback;
    using MessageSender = HttpMessage::MessageSender;
    using MessageVSender = HttpMessage::MessageVSender;
    using MessageBSender = HttpMessage::MessageBSender;
    
    H1xStream(const EventLoopPtr &loop);
    ~H1xStream();
    
    KMError setSslFlags(uint32_t ssl_flags) { return tcp_conn_.setSslFlags(ssl_flags); }
    KMError setProxyInfo(const ProxyInfo &proxy_info);
    KMError addHeader(std::string name, std::string value);
    KMError sendRequest(const std::string &method, const std::string &url, const std::string &ver);
    KMError attachFd(SOCKET_FD fd, const KMBuffer *init_buf);
    KMError attachSocket(TcpSocket::Impl&& tcp, HttpParser::Impl&& parser, const KMBuffer *init_buf);
    KMError sendResponse(int status_code, const std::string &desc, const std::string &ver);
    int sendData(const void* data, size_t len);
    int sendData(const KMBuffer &buf);
    void reset();
    KMError close();
    
    bool isServer() const { return tcp_conn_.isServer(); }
    bool canSendData() const { return tcp_conn_.canSendData(); }
    
    bool isOutgoingComplete() const { return outgoing_message_.isComplete(); }
    bool isIncomingComplete() const { return incoming_parser_.complete(); }
    
    HttpHeader& getOutgoingHeaders() { return outgoing_message_; }
    const HttpHeader& getOutgoingHeaders() const { return outgoing_message_; }
    HttpHeader& getIncomingHeaders() { return incoming_parser_; }
    const HttpHeader& getIncomingHeaders() const { return incoming_parser_; }
    const std::string& getMethod() const { return incoming_parser_.getMethod(); }
    const std::string& getPath() const { return incoming_parser_.getUrlPath(); }
    const std::string& getQuery() const { return incoming_parser_.getUrlQuery(); }
    const std::string& getParamValue(const std::string &name) const
    {
        return incoming_parser_.getParamValue(name);
    }
    int getStatusCode() const { return incoming_parser_.getStatusCode(); }
    const std::string& getVersion() const { return incoming_parser_.getVersion(); }
    
    void setHeaderCallback(HeaderCallback cb) { header_cb_ = std::move(cb); }
    void setDataCallback(DataCallback cb) { data_cb_ = std::move(cb); }
    void setWriteCallback(EventCallback cb) { write_cb_ = std::move(cb); }
    void setErrorCallback(EventCallback cb) { error_cb_ = std::move(cb); }
    void setIncomingCompleteCallback(CompleteCallback cb) { incoming_complete_cb_ = std::move(cb); }
    void setOutgoingCompleteCallback(CompleteCallback cb) { outgoing_complete_cb_ = std::move(cb); }
    
    template<typename Runnable> // (void)
    bool runOnLoopThread(Runnable &&r, bool maybe_sync=true)
    {
        auto loop = tcp_conn_.eventLoop();
        if (!loop || (maybe_sync && loop->inSameThread())) {
            r();
            return true;
        } else {
            return loop->post([r = std::move(r)]{
                r();
            }, &loop_token_) == kev::Result::OK;
        }
    }
    
protected: // callbacks of TcpConnection
    void onConnect(KMError err);
    KMError handleInputData(uint8_t *src, size_t len);
    void onWrite();
    void onError(KMError err);
    
protected:
    std::string buildRequest();
    std::string buildResponse(int status_code, const std::string &desc, const std::string &ver);
    KMError sendHeaders(const std::string &headers);
    
    void onHeaderComplete();
    void onStreamData(KMBuffer &buf);
    void onOutgoingComplete();
    void onIncomingComplete();
    void onStreamError(KMError err);
    
    void onHttpData(KMBuffer &buf);
    void onHttpEvent(HttpEvent ev);
    
protected:
    std::string             method_;
    Uri                     uri_;
    std::string             version_;
    HttpMessage             outgoing_message_;
    bool                    wait_outgoing_complete_ = false;
    HttpParser::Impl        incoming_parser_;
    bool                    is_stream_upgraded_ = false;
    
    HeaderCallback          header_cb_;
    DataCallback            data_cb_;
    EventCallback           write_cb_;
    EventCallback           error_cb_;
    CompleteCallback        incoming_complete_cb_;
    CompleteCallback        outgoing_complete_cb_;
    
    ProxyConnection::Impl   tcp_conn_;
    EventLoopToken          loop_token_;
};

KUMA_NS_END

