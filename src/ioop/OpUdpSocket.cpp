/* Copyright (c) 2014-2024, Fengping Bao <jamol@live.com>
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

#include "OpUdpSocket.h"
#include "libkev/src/utils/kmtrace.h"
#include "libkev/src/utils/skutils.h"

using namespace kuma;

const size_t kMaxPendingSendBytes = 1024*1024;
const size_t kMinPendingSendBytes = 32*1024;
const int kMaxPendingRecvOps = 1;
const size_t kUDPRecvPacketSize = 64 * 1024;

OpUdpSocket::OpUdpSocket(const EventLoopPtr &loop)
    : UdpSocketBase(loop), op_ctx_(OpContext::create())
{
    KM_SetObjKey("OpUdpSocket");
    //KM_INFOXTRACE("OpUdpSocket");
    op_ctx_->setRecvCallback([this] (int res, KMBuffer buf) {
        onReceive(res, buf);
    });
}

OpUdpSocket::~OpUdpSocket()
{
    //KM_INFOXTRACE("~OpUdpSocket");
    cleanup();
}

#if defined(KUMA_OS_WIN)
SOCKET_FD OpUdpSocket::createFd(int addr_family)
{
    return WSASocketW(addr_family, SOCK_DGRAM, IPPROTO_IP, NULL, 0, WSA_FLAG_OVERLAPPED);
    //return UdpSocketBase::createFd(addr_family);
}
#endif

bool OpUdpSocket::registerFd(SOCKET_FD fd)
{
    return op_ctx_->registerFd(loop_.lock(), fd);
}

void OpUdpSocket::unregisterFd(SOCKET_FD fd, bool close_fd)
{
    op_ctx_->unregisterFd(loop_.lock(), fd, close_fd);
}

int OpUdpSocket::receive(void *data, size_t length, char *ip, size_t ip_len, uint16_t &port)
{
    if (INVALID_FD == fd_) {
        KM_ERRXTRACE("receive, invalid fd");
        return -1;
    }
    if (recvBlocked()) {
        return 0;
    }
    
    if (!recv_buf_.empty()) {
        if (recv_buf_.size() > length) {
            return int(KMError::BUFFER_TOO_SMALL);
        }
        auto bytes_read = recv_buf_.read(data, length);
        auto ss_addr = op_ctx_->getSockAddr();
        auto addr_len = kev::km_get_addr_length(ss_addr);
        kev::km_get_sock_addr((struct sockaddr*)&ss_addr, addr_len, ip, (uint32_t)ip_len, &port);
        //KM_INFOXTRACE("receive, bytes_read=" << bytes_read<<", len="<<length);
        return static_cast<int>(bytes_read);
    }
    
    auto ret = UdpSocketBase::receive(data, length, ip, ip_len, port);
    if (ret == 0) {
        postRecvOp();
    }

    //KM_INFOXTRACE("receive, ret="<<ret<<", len="<<length);
    return ret;
}

bool OpUdpSocket::postRecvOp()
{
    if (pending_recv_ops_ >= kMaxPendingRecvOps) {
        return false;
    }
    if (recv_buf_.space() == 0) {
        recv_buf_.allocBuffer(kUDPRecvPacketSize);
    }
    auto ret = op_ctx_->postRecvMsgOp(loop_.lock(), fd_, recv_buf_);
    if (ret == KMError::NOERR) {
        ++pending_recv_ops_;
        return true;
    }
    return false;
}

void OpUdpSocket::onSocketInitialized()
{
    postRecvOp();
}

void OpUdpSocket::onReceive(int res, const KMBuffer &buf)
{
    if (res == 0
#if !defined(KUMA_OS_WIN)
        || res < 0
#endif
    ) {
        if (fd_ != INVALID_FD) {
            KM_WARNXTRACE("onReceive, res=" << res);
            cleanup();
            onClose(KMError::SOCK_ERROR);
        }
        return;
    }
    --pending_recv_ops_;
    recv_buf_ = buf;
    UdpSocketBase::onReceive(KMError::NOERR);
}
