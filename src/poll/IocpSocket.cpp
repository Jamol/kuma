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
#include "IOPoll.h"

# include <MSWSock.h>
# include <Ws2tcpip.h>
# include <windows.h>

using namespace kuma;

KUMA_NS_BEGIN
extern LPFN_CONNECTEX connect_ex;
extern LPFN_ACCEPTEX accept_ex;
KUMA_NS_END

IocpSocket::IocpSocket(const EventLoopPtr &loop)
    : SocketBase(loop)
{
    KM_SetObjKey("IocpSocket");
}

IocpSocket::~IocpSocket()
{
    cleanup();
}

KMError IocpSocket::connect_i(const sockaddr_storage &ss_addr, uint32_t timeout_ms)
{
    if (!connect_ex) {
        return KMError::UNSUPPORT;
    }
    if (INVALID_FD == fd_) {
        //fd_ = ::socket(ss_addr.ss_family, SOCK_STREAM, 0);
        fd_ = WSASocketW(ss_addr.ss_family, SOCK_STREAM, IPPROTO_IP, NULL, 0, WSA_FLAG_OVERLAPPED);
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

    int addr_len = km_get_addr_length(ss_addr);
    auto ret = connect_ex(fd_, (LPSOCKADDR)&ss_addr, addr_len, NULL, 0, NULL, &recv_ol_);
    if (ret) {
        setState(State::CONNECTING); // wait for writable event
    }
    else if (getLastError() == ERROR_IO_PENDING || getLastError() == WSA_IO_PENDING) {
        setState(State::CONNECTING);
    }
    else {
        KUMA_ERRXTRACE("connect_i, error, fd=" << fd_ << ", err=" << getLastError());
        cleanup();
        setState(State::CLOSED);
        return KMError::FAILED;
    }

#if defined(KUMA_OS_LINUX) || defined(KUMA_OS_MAC)
    socklen_t len = sizeof(ss_addr);
#else
    int len = sizeof(ss_addr);
#endif
    char local_ip[128] = { 0 };
    uint16_t local_port = 0;
    ret = getsockname(fd_, (struct sockaddr*)&ss_addr, &len);
    if (ret != -1) {
        km_get_sock_addr((struct sockaddr*)&ss_addr, sizeof(ss_addr), local_ip, sizeof(local_ip), &local_port);
    }

    KUMA_INFOXTRACE("connect_i, fd=" << fd_ << ", local_ip=" << local_ip
        << ", local_port=" << local_port << ", state=" << getState());

    return KMError::NOERR;
}

KMError IocpSocket::attachFd(SOCKET_FD fd)
{
    KUMA_INFOXTRACE("attachFd, fd=" << fd << ", state=" << getState());
    if (getState() != State::IDLE) {
        KUMA_ERRXTRACE("attachFd, invalid state, state=" << getState());
        return KMError::INVALID_STATE;
    }

    fd_ = fd;
    setSocketOption();
    setState(State::OPEN);
    registerFd(fd_);
    return KMError::NOERR;
}

void IocpSocket::registerFd(SOCKET_FD fd)
{
    auto loop = loop_.lock();
    if (loop && fd != INVALID_FD) {
        loop->registerFd(fd, KUMA_EV_NETWORK, [this](KMEvent ev, void* ol, size_t io_size) { ioReady(ev, ol, io_size); });
        registered_ = true;
    }
}

void IocpSocket::unregisterFd(SOCKET_FD fd, bool close_fd)
{
    if (registered_) {
        registered_ = false;
        auto loop = loop_.lock();
        if (loop && fd != INVALID_FD) {
            loop->unregisterFd(fd, close_fd);
        }
    }
    else if (close_fd) {
        closeFd(fd);
    }
}

int IocpSocket::send(const void* data, size_t length)
{
    iovec iov;
    iov.iov_base = (char*)data;
    iov.iov_len = length;
    return send(&iov, 1);
}

int IocpSocket::send(iovec* iovs, int count)
{
    if (!isReady()) {
        KUMA_WARNXTRACE("send, invalid state=" << getState());
        return 0;
    }
    if (INVALID_FD == fd_) {
        KUMA_ERRXTRACE("send, invalid fd");
        return -1;
    }

    if (!send_buf_.empty() || send_pending_) {
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
        for (size_t i = 0; i<count; ++i) {
            const uint8_t* first = ((uint8_t*)iovs[i].iov_base) + ret;
            const uint8_t* last = ((uint8_t*)iovs[i].iov_base) + iovs[i].iov_len;
            if (first < last) {
                send_buf_.write(first, last - first);
                ret = 0;
            }
            else {
                ret -= iovs[i].iov_len;
            }
        }
        postSendOperation();
    }

    //KUMA_INFOXTRACE("send, ret="<<ret<<", bytes_total="<<bytes_total);
    return ret < 0 ? ret : static_cast<int>(bytes_total);
}

int IocpSocket::receive(void* data, size_t length)
{
    if (!isReady()) {
        return 0;
    }
    if (INVALID_FD == fd_) {
        KUMA_ERRXTRACE("receive, invalid fd");
        return -1;
    }
    if (recv_pending_) {
        return 0;
    }
    char *ptr = (char*)data;
    size_t bytes_recv = 0;
    if (!recv_buf_.empty()) {
        auto bytes_read = recv_buf_.read(ptr + bytes_recv, length - bytes_recv);
        bytes_recv += bytes_read;
    }
    if (bytes_recv == length) {
        return static_cast<int>(bytes_recv);
    }
    auto ret = SocketBase::receive(ptr + bytes_recv, length - bytes_recv);
    if (ret >= 0) {
        bytes_recv += ret;
    }
    else {
        return ret;
    }

    if (bytes_recv == 0) {
        postRecvOperation();
    }

    //KUMA_INFOXTRACE("receive, ret="<<ret<<", bytes_recv="<<bytes_recv);
    return static_cast<int>(bytes_recv);;
}

KMError IocpSocket::close()
{
    KUMA_INFOXTRACE("close, state=" << getState());
    auto loop = loop_.lock();
    if (loop && !loop->stopped()) {
        loop->sync([this] {
            cleanup();
        });
    }
    else {
        cleanup();
    }
    setState(State::CLOSED);
    return KMError::NOERR;
}

void IocpSocket::notifySendBlocked()
{

}

KMError IocpSocket::pause()
{
    auto loop = loop_.lock();
    if (loop) {
        return loop->updateFd(fd_, KUMA_EV_ERROR);
    }
    return KMError::INVALID_STATE;
}

KMError IocpSocket::resume()
{
    auto loop = loop_.lock();
    if (loop) {
        return loop->updateFd(fd_, KUMA_EV_NETWORK);
    }
    return KMError::INVALID_STATE;
}

void IocpSocket::onConnect(KMError err)
{
    if (err == KMError::NOERR) {
        setsockopt(fd_, SOL_SOCKET, SO_UPDATE_CONNECT_CONTEXT, NULL, 0);
        postRecvOperation();
    }
    SocketBase::onConnect(err);
}

void IocpSocket::onSend(size_t io_size)
{
    if (io_size != send_buf_.size()) {
        KUMA_ERRXTRACE("onSend, error, io_size="<<io_size<<", buffer="<<send_buf_.size());
    }
    send_buf_.bytes_read(io_size);
    send_pending_ = false;
    SocketBase::onSend(KMError::NOERR);
}

void IocpSocket::onReceive(size_t io_size)
{
    if (io_size > recv_buf_.space()) {
        KUMA_ERRXTRACE("onReceive, error, io_size=" << io_size << ", buffer=" << recv_buf_.space());
    }
    recv_buf_.bytes_written(io_size);
    recv_pending_ = false;
    SocketBase::onReceive(KMError::NOERR);
}

void IocpSocket::onClose(KMError err)
{
    KUMA_INFOXTRACE("onClose, err=" << int(err) << ", state=" << getState());
    cleanup();
    setState(State::CLOSED);
    if (error_cb_) error_cb_(err);
}

int IocpSocket::postSendOperation()
{
    if (send_buf_.empty() || send_pending_) {
        return 0;
    }
    wsa_buf_s_.buf = (char*)send_buf_.ptr();
    wsa_buf_s_.len = send_buf_.size();
    DWORD bytes_sent = 0;
    auto ret = WSASend(fd_, &wsa_buf_s_, 1, &bytes_sent, 0, &send_ol_, NULL);
    if (0 == ret) ret = bytes_sent;
    if (ret == SOCKET_ERROR) {
        if (WSA_IO_PENDING == WSAGetLastError()) {
            send_pending_ = true;
            return 0;
        }
        return -1;
    }
    else if (ret > 0) {
        send_buf_.bytes_read(ret);
    }
    return ret;
}

int IocpSocket::postRecvOperation()
{
    if (recv_pending_) {
        return 0;
    }
    recv_buf_.expand(4096);
    wsa_buf_r_.buf = (char*)recv_buf_.wr_ptr();
    wsa_buf_r_.len = recv_buf_.space();
    DWORD bytes_recv = 0, flags = 0;
    auto ret = WSARecv(fd_, &wsa_buf_r_, 1, &bytes_recv, &flags, &recv_ol_, NULL);
    if (0 == ret) ret = bytes_recv;
    if (ret == SOCKET_ERROR) {
        if (WSA_IO_PENDING == WSAGetLastError()) {
            recv_pending_ = true;
            return 0;
        }
        return -1;
    }
    else if (ret > 0) {
        recv_buf_.bytes_written(ret);
    }
    return ret;
}

void IocpSocket::ioReady(KMEvent events, void* ol, size_t io_size)
{
    //KUMA_INFOXTRACE("ioReady, io_size="<< io_size);
    if (ol == &recv_ol_) {
        if (getState() == State::CONNECTING) {
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
        else {
            onReceive(io_size);
        }
    }
    else if (ol == &send_ol_) {
        onSend(io_size);
    }
}
