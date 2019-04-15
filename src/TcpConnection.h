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

#ifndef __TcpConnection_H__
#define __TcpConnection_H__

#include "kmdefs.h"
#include "TcpSocketImpl.h"
#include "util/skbuffer.h"

KUMA_NS_BEGIN

class TcpConnection
{
public:
    TcpConnection(const EventLoopPtr &loop);
	virtual ~TcpConnection();
    
    KMError setSslFlags(uint32_t ssl_flags);
    uint32_t getSslFlags() const { return tcp_.getSslFlags(); }
    bool sslEnabled() const { return tcp_.sslEnabled(); }
    KMError connect(const std::string &host, uint16_t port);
    KMError attachFd(SOCKET_FD fd, const KMBuffer *init_buf);
    KMError attachSocket(TcpSocket::Impl &&tcp, const KMBuffer *init_buf);
    int send(const void* data, size_t len);
    int send(const iovec* iovs, int count);
    int send(const KMBuffer &buf);
    KMError close();
    
    bool isServer() const { return isServer_; }
    bool isOpen() const { return tcp_.isReady(); }
    
#ifdef KUMA_HAS_OPENSSL
    KMError setAlpnProtocols(const AlpnProtos &protocols);
    KMError getAlpnSelected(std::string &protocol);
    KMError setSslServerName(std::string serverName);
#endif
    
    EventLoopPtr eventLoop() { return tcp_.eventLoop(); }
    
protected:
    // subclass should install destroy detector in this interface or implement delayed destroy
    // otherwise invalid memory accessing may happen on onReceive
    virtual KMError handleInputData(uint8_t *src, size_t len) = 0;
    virtual void onConnect(KMError err) {};
    virtual void onWrite() = 0;
    virtual void onError(KMError err) = 0;
    bool sendBufferEmpty() const { return !send_buffer_ || send_buffer_->empty(); }
    KMError sendBufferedData();
    void appendSendBuffer(const KMBuffer &buf);
    void reset();
    
private:
    void onSend(KMError err);
    void onReceive(KMError err);
    void onClose(KMError err);
    
private:
    void cleanup();
    void setupCallbacks();
    void saveInitData(const KMBuffer *init_buf);
    
protected:
    TcpSocket::Impl tcp_;
    std::string host_;
    uint16_t port_{ 0 };
    KMBuffer::Ptr send_buffer_;
    
private:
    std::vector<uint8_t>    initData_;
    
    bool                    isServer_{ false };
};

KUMA_NS_END

#endif
