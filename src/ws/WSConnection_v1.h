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
#include "http/H1xStream.h"
#include "libkev/src/utils/DestroyDetector.h"
#include "kmbuffer.h"

WS_NS_BEGIN

class WSConnection_V1 : public kev::KMObject, public kev::DestroyDetector, public WSConnection
{
public:
    WSConnection_V1(const EventLoopPtr &loop);
    ~WSConnection_V1();
    
    KMError setSslFlags(uint32_t ssl_flags) override
    {
        return stream_->setSslFlags(ssl_flags);
    }
    KMError setProxyInfo(const ProxyInfo &proxy_info) override;
    KMError addHeader(std::string name, std::string value) override;
    KMError addHeader(std::string name, uint32_t value) override;
    KMError connect(const std::string& ws_url) override;
    KMError attachFd(SOCKET_FD fd, const KMBuffer *init_buf, HandshakeCallback cb);
    KMError attachSocket(TcpSocket::Impl&& tcp,
                         HttpParser::Impl&& parser,
                         const KMBuffer *init_buf,
                         HandshakeCallback cb);
    int send(const iovec* iovs, int count) override;
    KMError close() override;
    bool canSendData() const override;
    const std::string& getPath() const override
    {
        return stream_->getPath();
    }
    const HttpHeader& getHeaders() const override
    {
        return stream_->getIncomingHeaders();
    }
    
protected:
    void onWrite();
    void onError(KMError err);
    void onHeader();
    void onData(KMBuffer &buf);
    
protected:
    void onStateError(KMError err) override;
    
    KMError connect_i(const std::string& ws_url);
    void cleanup();
    
    KMError sendUpgradeResponse(int status_code, const std::string &desc);
    
    void handleUpgradeRequest();
    void handleUpgradeResponse();
    void checkHandshake();
    
protected:
    std::unique_ptr<H1xStream> stream_;
    std::string             sec_ws_key_;
};

WS_NS_END
