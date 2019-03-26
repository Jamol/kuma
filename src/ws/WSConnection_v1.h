/* Copyright (c) 2014-2019, Fengping Bao <jamol@live.com>
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

#include "WSConnection.h"
#include "TcpConnection.h"
#include "http/HttpParserImpl.h"
#include "http/Uri.h"
#include "util/DestroyDetector.h"
#include "kmbuffer.h"

WS_NS_BEGIN

class WSConnection_V1 : public KMObject, public DestroyDetector, public WSConnection, public TcpConnection
{
public:
    WSConnection_V1(const EventLoopPtr &loop);
    ~WSConnection_V1();
    
    KMError setSslFlags(uint32_t ssl_flags) override
    {
        return TcpConnection::setSslFlags(ssl_flags);
    }
    KMError addHeader(std::string name, std::string value) override;
    KMError addHeader(std::string name, uint32_t value) override;
    KMError connect(const std::string& ws_url) override;
    KMError attachFd(SOCKET_FD fd, const KMBuffer *init_buf, HandshakeCallback cb);
    KMError attachSocket(TcpSocket::Impl&& tcp,
                         HttpParser::Impl&& parser,
                         const KMBuffer *init_buf,
                         HandshakeCallback cb);
    int send(const iovec* iovs, int count) override
    {
        return TcpConnection::send(iovs, count);
    }
    KMError close() override;
    bool canSendData() const override;
    const std::string& getPath() const override
    {
        return http_parser_.getUrlPath();
    }
    const HttpHeader& getHeaders() const override
    {
        return http_parser_;
    }
    
protected:
    // TcpConnection
    void onConnect(KMError err) override;
    KMError handleInputData(uint8_t *src, size_t len) override;
    void onWrite() override;
    void onError(KMError err) override;
    
    // HttpParser
    void onHttpData(KMBuffer &buf);
    void onHttpEvent(HttpEvent ev);
    
    void onWsData(uint8_t *src, size_t len);
    
protected:
    void onStateError(KMError err) override;
    
    KMError connect_i(const std::string& ws_url);
    void cleanup();
    
    std::string buildUpgradeRequest(const std::string &origin,
                                    const std::string &subprotocol,
                                    const std::string &extensions,
                                    const HeaderVector &header_vec);
    std::string buildUpgradeResponse(KMError result,
                                     const std::string &subprotocol,
                                     const std::string &extensions,
                                     const HeaderVector &header_vec);
    KMError sendUpgradeRequest(const std::string &origin,
                               const std::string &subprotocol,
                               const std::string &extensions,
                               const HeaderVector &header_vec);
    KMError sendUpgradeResponse(KMError result,
                                const std::string &subprotocol,
                                const std::string &extensions,
                                const HeaderVector &header_vec);
    
    void handleUpgradeRequest();
    void handleUpgradeResponse();
    void checkHandshake();
    
protected:
    Uri                     uri_;
    HttpParser::Impl        http_parser_;
    size_t                  body_bytes_sent_ = 0;
    std::string             sec_ws_key_;
    KMError                 handshake_result_ = KMError::FAILED;
    HttpHeader              outgoing_header_ {true, false};
    
    EventLoopToken          loop_token_;
};

WS_NS_END
