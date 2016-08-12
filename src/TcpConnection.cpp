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

#include "TcpConnection.h"
#include "util/kmtrace.h"

#include <sstream>

using namespace kuma;

//////////////////////////////////////////////////////////////////////////
TcpConnection::TcpConnection(EventLoopImpl* loop)
: loop_(loop), tcp_(loop)
{
    
}

TcpConnection::~TcpConnection()
{
    
}

void TcpConnection::cleanup()
{
    tcp_.close();
}

int TcpConnection::setSslFlags(uint32_t ssl_flags)
{
    return tcp_.setSslFlags(ssl_flags);
}

void TcpConnection::setupCallbacks()
{
    tcp_.setReadCallback([this] (int err) { onReceive(err); });
    tcp_.setWriteCallback([this] (int err) { onSend(err); });
    tcp_.setErrorCallback([this] (int err) { onClose(err); });
}

int TcpConnection::connect(const std::string &host, uint16_t port)
{
    isServer_ = false;
    host_ = host;
    port_ = port;
    setupCallbacks();
    
    return tcp_.connect(host.c_str(), port, [this] (int err) { onConnect(err); });
}

int TcpConnection::attachFd(SOCKET_FD fd, const uint8_t* data, size_t size)
{
    isServer_ = true;
    if(data && size > 0) {
        initData_.assign(data, data + size);
    }
    setupCallbacks();
    
    return tcp_.attachFd(fd);
}

int TcpConnection::attachSocket(TcpSocketImpl&& tcp)
{
    isServer_ = true;
    setupCallbacks();
    
    return tcp_.attach(std::move(tcp));
}

int TcpConnection::send(const uint8_t* data, size_t len)
{
    if(!sendBufferEmpty()) {
        // try to send buffered data
        auto ret = sendBufferedData();
        if (ret != KUMA_ERROR_NOERR) {
            return -1;
        } else if (!sendBufferEmpty()) {
            return 0;
        }
    }
    int ret = tcp_.send(data, len);
    if (ret >= 0 && ret < len) {
        send_buffer_.assign(data + ret, data + len);
        send_offset_ = 0;
    }
    return int(len);
}

int TcpConnection::send(iovec* iovs, int count)
{
    if(!sendBufferEmpty()) {
        return 0;
    }
    int ret = tcp_.send(iovs, count);
    if (ret >= 0) {
        size_t total_len = 0;
        for (size_t i=0; i<count; ++i) {
            total_len += iovs[i].iov_len;
            const uint8_t* first = ((uint8_t*)iovs[i].iov_base) + ret;
            const uint8_t* last = ((uint8_t*)iovs[i].iov_base) + iovs[i].iov_len;
            if(first < last) {
                send_buffer_.insert(send_buffer_.end(), first, last);
                ret = 0;
            } else {
                ret -= iovs[i].iov_len;
            }
        }
        send_offset_ = 0;
        return int(total_len);
    }
    return ret;
}

int TcpConnection::close()
{
    //KUMA_INFOXTRACE("close");
    cleanup();
    return KUMA_ERROR_NOERR;
}

KMError TcpConnection::sendBufferedData()
{
    if(!send_buffer_.empty() && send_offset_ < send_buffer_.size()) {
        int ret = tcp_.send(&send_buffer_[0] + send_offset_, send_buffer_.size() - send_offset_);
        if(ret < 0) {
            return KUMA_ERROR_SOCKERR;
        } else {
            send_offset_ += ret;
            if(send_offset_ == send_buffer_.size()) {
                send_offset_ = 0;
                send_buffer_.clear();
            }
        }
    }
    return KUMA_ERROR_NOERR;
}

void TcpConnection::onSend(int err)
{
    if (sendBufferedData() != KUMA_ERROR_NOERR) {
        cleanup();
        onError(KUMA_ERROR_SOCKERR);
        return;
    }
    if (sendBufferEmpty()) {
        onWrite();
    }
}

void TcpConnection::onReceive(int err)
{
    if(!initData_.empty()) {
        int ret = handleInputData(&initData_[0], initData_.size());
        if (ret != KUMA_ERROR_NOERR) {
            return;
        }
        initData_.clear();
    }
    uint8_t buf[128*1024];
    do {
        int ret = tcp_.receive(buf, sizeof(buf));
        if (ret > 0) {
            if (handleInputData(buf, ret) != KUMA_ERROR_NOERR) {
                break;
            }
        } else if (0 == ret) {
            break;
        } else { // ret < 0
            cleanup();
            onError(KUMA_ERROR_SOCKERR);
            return;
        }
    } while(true);
}

void TcpConnection::onClose(int err)
{
    //KUMA_INFOXTRACE("onClose");
    cleanup();
    onError(err);
}
