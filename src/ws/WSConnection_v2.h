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
#include "EventLoopImpl.h"
#include "http/v2/H2ConnectionImpl.h"
#include "http/Uri.h"
#include "util/DestroyDetector.h"

KUMA_NS_BEGIN
class H2StreamProxy;
KUMA_NS_END

WS_NS_BEGIN


class WSConnection_V2 : public KMObject, public DestroyDetector, public WSConnection
{
public:
    WSConnection_V2(const EventLoopPtr &loop);
    ~WSConnection_V2();
    
    KMError setSslFlags(uint32_t ssl_flags) override;
    KMError addHeader(std::string name, std::string value) override;
    KMError addHeader(std::string name, uint32_t value) override;
    KMError connect(const std::string& ws_url) override;
    KMError attachStream(uint32_t stream_id, H2Connection::Impl* conn, HandshakeCallback cb);
    int send(const iovec* iovs, int count) override;
    KMError close() override;
    bool canSendData() const override;
    
    const std::string& getPath() const override;
    const HttpHeader& getHeaders() const override;
    
protected:
    void handleHandshakeRequest();
    void handleHandshakeResponse();
    void checkHandshake();
    KMError sendResponse(int status_code);
    
protected:
    void onError(KMError err);
    void onHeader(bool end_stream);
    void onData(KMBuffer &buf, bool end_stream);
    void onWrite();
    void onComplete();
    
protected:
    uint32_t ssl_flags_ = 0;
    std::unique_ptr<H2StreamProxy> stream_;
};

WS_NS_END
