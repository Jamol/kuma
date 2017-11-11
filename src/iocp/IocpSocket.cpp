/* Copyright (c) 2014-2017, Fengping Bao <jamol@live.com>
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

#include "IocpSocket.h"
#include "util/kmtrace.h"

#include <MSWSock.h>
#include <Ws2tcpip.h>
#include <windows.h>

using namespace kuma;

IocpSocket::IocpSocket(const EventLoopPtr &loop)
    : SocketBase(loop), IocpBase(IocpWrapperPtr(new IocpWrapper()))
{
    KM_SetObjKey("IocpSocket");
    //KUMA_INFOXTRACE("IocpSocket");
}

IocpSocket::~IocpSocket()
{
    //KUMA_INFOXTRACE("~IocpSocket");
    cleanup();
}

SOCKET_FD IocpSocket::createFd(int addr_family)
{
    return WSASocketW(addr_family, SOCK_STREAM, IPPROTO_IP, NULL, 0, WSA_FLAG_OVERLAPPED);
}

bool IocpSocket::registerFd(SOCKET_FD fd)
{
    return IocpBase::registerFd(loop_.lock(), fd);
}

void IocpSocket::unregisterFd(SOCKET_FD fd, bool close_fd)
{
    IocpBase::unregisterFd(loop_.lock(), fd, close_fd);
}

KMError IocpSocket::connect_i(const sockaddr_storage &ss_addr, uint32_t timeout_ms)
{
    if (!connect_ex) {
        return KMError::UNSUPPORT;
    }
    if (INVALID_FD == fd_) {
        fd_ = createFd(ss_addr.ss_family);
        if (INVALID_FD == fd_) {
            KUMA_ERRXTRACE("connect_i, socket failed, err=" << getLastError());
            return KMError::FAILED;
        }
        // need bind before ConnectEx
        sockaddr_storage ss_any = { 0 };
        ss_any.ss_family = ss_addr.ss_family;
        int addr_len = km_get_addr_length(ss_any);
        int ret = ::bind(fd_, (struct sockaddr*)&ss_any, addr_len);
        if (ret < 0) {
            KUMA_ERRXTRACE("connect_i, bind failed, err=" << getLastError());
        }
    }
    setSocketOption();
    registerFd(fd_);

    if (!postConnectOperation(fd_, ss_addr)) {
        cleanup();
        setState(State::CLOSED);
        return KMError::FAILED;
    }
    setState(State::CONNECTING);

#if defined(KUMA_OS_LINUX) || defined(KUMA_OS_MAC)
    socklen_t len = sizeof(ss_addr);
#else
    int len = sizeof(ss_addr);
#endif
    char local_ip[128] = { 0 };
    uint16_t local_port = 0;
    auto ret = getsockname(fd_, (struct sockaddr*)&ss_addr, &len);
    if (ret != -1) {
        km_get_sock_addr((struct sockaddr*)&ss_addr, sizeof(ss_addr), local_ip, sizeof(local_ip), &local_port);
    }

    KUMA_INFOXTRACE("connect_i, fd=" << fd_ << ", local_ip=" << local_ip
        << ", local_port=" << local_port << ", state=" << getState());

    return KMError::NOERR;
}

KMError IocpSocket::attachFd(SOCKET_FD fd)
{
    SocketBase::attachFd(fd);
    postRecvOperation(fd_);
    return KMError::NOERR;
}

KMError IocpSocket::detachFd(SOCKET_FD &fd)
{
    // cannot cancel the IO safely
    return KMError::UNSUPPORT;
    /*unregisterFd(fd_, false);
    return SocketBase::detachFd(fd);*/
}

int IocpSocket::send(const void* data, size_t length)
{
    iovec iov;
    iov.iov_base = (char*)data;
    iov.iov_len = static_cast<decltype(iov.iov_len)>(length);
    return send(&iov, 1);
}

int IocpSocket::send(const iovec* iovs, int count)
{
    if (!isReady()) {
        KUMA_WARNXTRACE("send, invalid state=" << getState());
        return 0;
    }
    if (sendPending()) {
        return 0;
    }

    size_t bytes_total = 0;
    for (int i = 0; i < count; ++i) {
        bytes_total += iovs[i].iov_len;
    }
    if (bytes_total == 0) {
        return 0;
    }

    DWORD bytes_sent = 0;
    auto ret = ::WSASend(fd_, (LPWSABUF)iovs, count, &bytes_sent, 0, NULL, NULL);
    if (0 == ret) ret = bytes_sent;

    if (0 == ret) {
        KUMA_WARNXTRACE("send, peer closed");
        ret = -1;
    }
    else if (ret < 0) {
        if (WSAEWOULDBLOCK == getLastError() || WSA_IO_PENDING == getLastError()) {
            ret = 0;
        }
        else {
            KUMA_ERRXTRACE("send, fail, err=" << getLastError());
        }
    }

    if (ret < 0) {
        cleanup();
        setState(State::CLOSED);
    }
    else if (static_cast<size_t>(ret) < bytes_total) {
        for (size_t i = 0; i < count; ++i) {
            const uint8_t* first = ((uint8_t*)iovs[i].iov_base) + ret;
            const uint8_t* last = ((uint8_t*)iovs[i].iov_base) + iovs[i].iov_len;
            if (first < last) {
                sendBuffer().write(first, last - first);
                ret = 0;
            }
            else {
                ret -= iovs[i].iov_len;
            }
        }
        postSendOperation(fd_);
    }

    //KUMA_INFOXTRACE("send, ret="<<ret<<", bytes_total="<<bytes_total);
    return ret < 0 ? ret : static_cast<int>(bytes_total);
}

int IocpSocket::receive(void* data, size_t length)
{
    if (!isReady()) {
        return 0;
    }
    if (recvPending()) {
        return 0;
    }
    char *ptr = (char*)data;
    size_t bytes_recv = 0;
    if (!recvBuffer().empty()) {
        auto bytes_read = recvBuffer().read(ptr + bytes_recv, length - bytes_recv);
        //KUMA_INFOXTRACE("receive, bytes_read=" << bytes_read<<", len="<<length);
        bytes_recv += bytes_read;
    }
    if (bytes_recv == length || !readable_) {
        if (!readable_) {
            postRecvOperation(fd_);
        }
        return static_cast<int>(bytes_recv);
    }
    auto ret = SocketBase::receive(ptr + bytes_recv, length - bytes_recv);
    if (ret >= 0) {
        bytes_recv += ret;
        if (bytes_recv < length) {
            postRecvOperation(fd_);
        }
    }
    else {
        return ret;
    }

    //KUMA_INFOXTRACE("receive, ret="<<ret<<", bytes_recv="<<bytes_recv<<", len="<<length);
    return static_cast<int>(bytes_recv);
}

KMError IocpSocket::pause()
{
    paused_ = true;
    return KMError::NOERR;
}

KMError IocpSocket::resume()
{
    paused_ = false;
    if (!recvBuffer().empty() || !recvPending()) {
        auto loop = eventLoop();
        if (loop) {
            loop->post([this] {
                SocketBase::onReceive(KMError::NOERR);
            });
        }
    }
    return KMError::NOERR;
}

void IocpSocket::onConnect(KMError err)
{
    if (err == KMError::NOERR) {
        setsockopt(fd_, SOL_SOCKET, SO_UPDATE_CONNECT_CONTEXT, NULL, 0);
        postRecvOperation(fd_);
    }
    SocketBase::onConnect(err);
}

void IocpSocket::onSend(size_t io_size)
{
    if (io_size == 0) {
        KUMA_WARNXTRACE("onSend, io_size=0, state=" << getState() << ", pending=" << hasPendingOperation());
        if (getState() == State::OPEN) {
            onClose(KMError::SOCK_ERROR);
        }
        else {
            cleanup();
        }
        return;
    }
    if (getState() != State::OPEN) {
        KUMA_WARNXTRACE("onSend, invalid state, state=" << getState() << ", io_size=" << io_size);
    }
    SocketBase::onSend(KMError::NOERR);
}

void IocpSocket::onReceive(size_t io_size)
{
    if (io_size == 0) {
        KUMA_WARNXTRACE("onReceive, io_size=0, state=" << getState() << ", pending=" << hasPendingOperation());
        if (getState() == State::OPEN) {
            onClose(KMError::SOCK_ERROR);
        }
        else {
            cleanup();
        }
        return;
    }
    if (getState() != State::OPEN) {
        KUMA_WARNXTRACE("onReceive, invalid state, state=" << getState() << ", io_size=" << io_size);
    }
    readable_ = recvBuffer().space() == 0;
    if (!paused_) {
        SocketBase::onReceive(KMError::NOERR);
    }
}

void IocpSocket::ioReady(IocpContext::Op op, size_t io_size)
{
    //KUMA_INFOXTRACE("ioReady, op="<<int(op)<<", io_size="<< io_size<<", state="<<getState());
    if (op == IocpContext::Op::CONNECT) {
        DWORD seconds;
        int bytes = sizeof(seconds);
        auto ret = getsockopt(fd_, SOL_SOCKET, SO_CONNECT_TIME, (char *)&seconds, (PINT)&bytes);
        if (ret != NO_ERROR || seconds == 0xFFFFFFFF) {
            onConnect(KMError::SOCK_ERROR);
        }
        else {
            onConnect(KMError::NOERR);
        }
    }
    else if (op == IocpContext::Op::RECV) {
        onReceive(io_size);
    }
    else if (op == IocpContext::Op::SEND) {
        onSend(io_size);
    }
    else {
        KUMA_WARNXTRACE("ioReady, invalid op: "<<int(op));
    }
}
