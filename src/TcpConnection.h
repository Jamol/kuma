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

#pragma once

#include "kmdefs.h"
#include "TcpSocketImpl.h"

KUMA_NS_BEGIN

class TcpConnection
{
public:
    using EventCallback = TcpSocket::EventCallback;
    using DataCallback = std::function<KMError(uint8_t*, size_t)>;

    TcpConnection(const EventLoopPtr &loop);
	virtual ~TcpConnection();
    
    KMError setSslFlags(uint32_t ssl_flags) { return tcp_.setSslFlags(ssl_flags); }
    uint32_t getSslFlags() const { return tcp_.getSslFlags(); }
    bool sslEnabled() const { return tcp_.sslEnabled(); }
    virtual KMError connect(const std::string &host, uint16_t port, EventCallback cb);
    KMError attachFd(SOCKET_FD fd, const KMBuffer *init_buf);
    KMError attachSocket(TcpSocket::Impl &&tcp, const KMBuffer *init_buf);
    int send(const void* data, size_t len);
    int send(const iovec* iovs, int count);
    int send(const KMBuffer &buf);
    KMError close();
    void reset();
    
    virtual void setDataCallback(DataCallback cb) { data_cb_ = std::move(cb); }
    virtual void setWriteCallback(EventCallback cb) { write_cb_ = std::move(cb); }
    virtual void setErrorCallback(EventCallback cb) { error_cb_ = std::move(cb); }

    bool isServer() const { return isServer_; }
    bool isOpen() const { return tcp_.isReady(); }
    bool canSendData() const { return isOpen() && sendBufferEmpty(); }
    
    void appendSendBuffer(const KMBuffer &buf);
    bool sendBufferEmpty() const { return !send_buffer_ || send_buffer_->empty(); }
    
#ifdef KUMA_HAS_OPENSSL
    KMError setAlpnProtocols(const AlpnProtos &protocols) { return tcp_.setAlpnProtocols(protocols); }
    KMError getAlpnSelected(std::string &protocol) { return tcp_.getAlpnSelected(protocol); }
    KMError setSslServerName(std::string serverName) { return tcp_.setSslServerName(std::move(serverName)); }
#endif
    
    EventLoopPtr eventLoop() { return tcp_.eventLoop(); }
    
protected:
    KMError sendBufferedData();
    
private:
    void onSend(KMError err);
    void onReceive(KMError err);
    void onClose(KMError err);
    void onError(KMError err);
    
private:
    void cleanup();
    void saveInitData(const KMBuffer *init_buf);
    
protected:
    TcpSocket::Impl tcp_;
    std::string host_;
    uint16_t port_{ 0 };
    KMBuffer::Ptr send_buffer_;
    
private:
    std::vector<uint8_t>    initData_;
    
    bool                    isServer_{ false };

    DataCallback            data_cb_;
    EventCallback           write_cb_;
    EventCallback           error_cb_;
};

KUMA_NS_END
