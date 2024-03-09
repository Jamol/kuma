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

#include "OpSocket.h"
#include "libkev/src/utils/kmtrace.h"
#include "libkev/src/utils/skutils.h"

using namespace kuma;

const size_t kMaxPendingSendBytes = 1024*1024;
const size_t kMinPendingSendBytes = 32*1024;
const int kMaxPendingRecvOps = 1;
const size_t kTCPRecvPacketSize = 4 * 1024;

OpSocket::OpSocket(const EventLoopPtr &loop)
    : SocketBase(loop), op_ctx_(OpContext::create())
{
    KM_SetObjKey("OpSocket");
    //KM_INFOXTRACE("OpSocket");
    op_ctx_->setConnectCallback([this] (int res) {
        onConnect(res);
    });
    op_ctx_->setSendCallback([this] (int res) {
        onSend(res);
    });
    op_ctx_->setRecvCallback([this] (int res, KMBuffer buf) {
        onReceive(res, buf);
    });
}

OpSocket::~OpSocket()
{
    //KM_INFOXTRACE("~OpSocket");
    cleanup();
}

#if defined(KUMA_OS_WIN)
SOCKET_FD OpSocket::createFd(int addr_family)
{
    return WSASocketW(addr_family, SOCK_STREAM, IPPROTO_IP, NULL, 0, WSA_FLAG_OVERLAPPED);
}
#endif

bool OpSocket::registerFd(SOCKET_FD fd)
{
    return op_ctx_->registerFd(loop_.lock(), fd);
}

void OpSocket::unregisterFd(SOCKET_FD fd, bool close_fd)
{
    op_ctx_->unregisterFd(loop_.lock(), fd, close_fd);
}

KMError OpSocket::connect_i(const sockaddr_storage &ss_addr, uint32_t timeout_ms)
{
    if (INVALID_FD == fd_) {
        fd_ = createFd(ss_addr.ss_family);
        if (INVALID_FD == fd_) {
            KM_ERRXTRACE("connect_i, socket failed, err=" << kev::SKUtils::getLastError());
            return KMError::FAILED;
        }
        // need bind before ConnectEx
        sockaddr_storage ss_any = { 0 };
        ss_any.ss_family = ss_addr.ss_family;
        auto addr_len = static_cast<int>(kev::km_get_addr_length(ss_any));
        int ret = ::bind(fd_, (struct sockaddr*)&ss_any, addr_len);
        if (ret < 0) {
            KM_ERRXTRACE("connect_i, bind failed, err=" << kev::SKUtils::getLastError());
        }
    }
    setSocketOption();
    if (!registerFd(fd_)) {
        KM_ERRXTRACE("connect_i, failed to register fd, fd=" << fd_);
        cleanup();
        setState(State::CLOSED);
        return KMError::FAILED;
    }

    auto op_ret = op_ctx_->postConnectOp(loop_.lock(), fd_, ss_addr);
    if (op_ret != KMError::NOERR) {
        cleanup();
        setState(State::CLOSED);
        return op_ret;
    }
    setState(State::CONNECTING);

    KM_INFOXTRACE("connect_i, fd=" << fd_ << ", state=" << getState());

    return KMError::NOERR;
}

KMError OpSocket::attachFd(SOCKET_FD fd)
{
    auto ret = SocketBase::attachFd(fd);
    if (ret == KMError::NOERR) {
        postRecvOp();
    }
    return ret;
}

KMError OpSocket::detachFd(SOCKET_FD &fd)
{
    // cannot cancel the IO safely
    return KMError::NOT_SUPPORTED;
    /*unregisterFd(fd_, false);
    return SocketBase::detachFd(fd);*/
}

int OpSocket::send(const void* data, size_t length)
{
    KMBuffer buf(data, length, length);
    return send(buf);
}

int OpSocket::send(const iovec* iovs, int count)
{
    if (!isReady()) {
        KM_WARNXTRACE("send, invalid state=" << getState());
        return 0;
    }
    if (send_blocked_) {
        return 0;
    }

    size_t bytes_total = 0;
    for (int i = 0; i < count; ++i) {
        bytes_total += iovs[i].iov_len;
    }
    if (bytes_total == 0) {
        return 0;
    }

    KMBuffer buf(bytes_total);
    for (int i = 0; i < count; ++i) {
        buf.write(iovs[i].iov_base, iovs[i].iov_len);
    }

    return send(buf);
}

int OpSocket::send(const KMBuffer &buf)
{
    if (!isReady()) {
        KM_WARNXTRACE("send, invalid state=" << getState());
        return 0;
    }
    if (send_blocked_) {
        return 0;
    }

    size_t bytes_total = buf.chainLength();
    if (bytes_total == 0) {
        return 0;
    }
    auto ret = op_ctx_->postSendOp(loop_.lock(), fd_, buf);
    if (ret != KMError::NOERR) {
        cleanup();
        setState(State::CLOSED);
        return -1;
    }
    pending_send_bytes_ += bytes_total;
    ++pending_send_ops_;
    if (pending_send_bytes_ >= kMaxPendingSendBytes) {
        send_blocked_ = true;
    }
    return static_cast<int>(bytes_total);
}

int OpSocket::receive(void* data, size_t length)
{
    if (!isReady()) {
        return 0;
    }
    if (recvBlocked()) {
        return 0;
    }
    char *ptr = (char*)data;
    size_t bytes_recv = 0;
    if (!recv_buf_.empty()) {
        auto bytes_read = recv_buf_.read(ptr + bytes_recv, length - bytes_recv);
        //KM_INFOXTRACE("receive, bytes_read=" << bytes_read<<", len="<<length);
        bytes_recv += bytes_read;
        if (recv_buf_.empty()) {
            recv_buf_.clear();
        }
    }
    if (bytes_recv == length || !readable_) {
        if (!readable_ && recv_buf_.empty()) {
            postRecvOp();
        }
        return static_cast<int>(bytes_recv);
    }
    auto ret = SocketBase::receive(ptr + bytes_recv, length - bytes_recv);
    if (ret >= 0) {
        bytes_recv += ret;
        if (bytes_recv < length) {
            postRecvOp();
        }
    }
    else {
        return ret;
    }

    //KM_INFOXTRACE("receive, ret="<<ret<<", bytes_recv="<<bytes_recv<<", len="<<length);
    return static_cast<int>(bytes_recv);
}

KMError OpSocket::pause()
{
    paused_ = true;
    return KMError::NOERR;
}

KMError OpSocket::resume()
{
    paused_ = false;
    if (pending_recv_ops_ < kMaxPendingRecvOps) {
        postRecvOp();
    }
    return KMError::NOERR;
}

bool OpSocket::postRecvOp()
{
    if (pending_recv_ops_ >= kMaxPendingRecvOps) {
        return false;
    }
    if (recv_buf_.space() == 0) {
        recv_buf_.allocBuffer(kTCPRecvPacketSize);
    }
    auto ret = op_ctx_->postRecvOp(loop_.lock(), fd_, recv_buf_);
    if (ret == KMError::NOERR) {
        ++pending_recv_ops_;
        return true;
    }
    return false;
}

void OpSocket::onConnect(int res)
{
#if defined(KUMA_OS_WIN)
    DWORD seconds;
    int bytes = sizeof(seconds);
    auto ret = getsockopt(fd_, SOL_SOCKET, SO_CONNECT_TIME, (char *)&seconds, (PINT)&bytes);
    if (ret != NO_ERROR || seconds == 0xFFFFFFFF) {
        SocketBase::onConnect(KMError::SOCK_ERROR);
        return;
    }
    setsockopt(fd_, SOL_SOCKET, SO_UPDATE_CONNECT_CONTEXT, NULL, 0);
#endif
    postRecvOp();

    sockaddr_storage ss_local;
    socklen_t len = sizeof(ss_local);
    char local_ip[128] = { 0 };
    uint16_t local_port = 0;
    auto ret = getsockname(fd_, (struct sockaddr*)&ss_local, &len);
    if (ret != -1) {
        kev::km_get_sock_addr((struct sockaddr*)&ss_local, sizeof(ss_local), local_ip, sizeof(local_ip), &local_port);
    }

    KM_INFOXTRACE("onConnect, fd=" << fd_ << ", local_ip=" << local_ip
        << ", local_port=" << local_port << ", state=" << getState() << ", res=" << res);
    
    SocketBase::onConnect(KMError::NOERR);
}

void OpSocket::onSend(int res)
{
    if (res == 0
#if !defined(KUMA_OS_WIN)
        || res < 0
#endif
    ) {
        KM_WARNXTRACE("onSend, res=" << res << ", state=" << getState() << ", pending=" << pending_send_bytes_);
        if (getState() == State::OPEN) {
            onClose(KMError::SOCK_ERROR);
        }
        else {
            cleanup();
        }
        return;
    }
    if (getState() != State::OPEN) {
        KM_WARNXTRACE("onSend, invalid state, state=" << getState() << ", res=" << res);
    }
    pending_send_bytes_ -= (uint32_t)res;
    --pending_send_ops_;
    if (send_blocked_ && pending_send_bytes_ < kMinPendingSendBytes) {
        send_blocked_ = false;
        SocketBase::onSend(KMError::NOERR);
    }
}

void OpSocket::onReceive(int res, const KMBuffer &buf)
{
    if (res == 0
#if !defined(KUMA_OS_WIN)
        || res < 0
#endif
    ) {
        KM_WARNXTRACE("onReceive, res=" << res << ", state=" << getState());
        if (getState() == State::OPEN) {
            onClose(KMError::SOCK_ERROR);
        }
        else {
            cleanup();
        }
        return;
    }
    if (getState() != State::OPEN) {
        KM_WARNXTRACE("onReceive, invalid state, state=" << getState() << ", res=" << res);
    }
    readable_ = buf.space() == 0;
    --pending_recv_ops_;
    recv_buf_ = buf;
    if (!paused_) {
        SocketBase::onReceive(KMError::NOERR);
    }
}
