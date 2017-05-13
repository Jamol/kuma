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

#include "PollSocket.h"

#if defined(KUMA_OS_WIN)
# include <Ws2tcpip.h>
# include <windows.h>
# include <time.h>
#elif defined(KUMA_OS_LINUX)
# include <string.h>
# include <pthread.h>
# include <unistd.h>
# include <fcntl.h>
# include <sys/types.h>
# include <sys/stat.h>
# include <sys/time.h>
# include <sys/socket.h>
# include <netdb.h>
# include <arpa/inet.h>
# include <netinet/tcp.h>
# include <netinet/in.h>
# ifdef KUMA_OS_ANDROID
#  include <sys/uio.h>
# endif
#elif defined(KUMA_OS_MAC)
# include <string.h>
# include <pthread.h>
# include <unistd.h>
# include <sys/types.h>
# include <sys/socket.h>
# include <sys/ioctl.h>
# include <sys/fcntl.h>
# include <sys/time.h>
# include <sys/uio.h>
# include <netinet/tcp.h>
# include <netinet/in.h>
# include <arpa/inet.h>
# include <netdb.h>
# include <ifaddrs.h>
#else
# error "UNSUPPORTED OS"
#endif

using namespace kuma;

PollSocket::PollSocket(const EventLoopPtr &loop)
    : SocketBase(loop)
{
    KM_SetObjKey("PollSocket");
}

PollSocket::~PollSocket()
{
    cleanup();
}

KMError PollSocket::connect_i(const sockaddr_storage &ss_addr, uint32_t timeout_ms)
{
    if (INVALID_FD == fd_) {
        fd_ = ::socket(ss_addr.ss_family, SOCK_STREAM, 0);
        if (INVALID_FD == fd_) {
            KUMA_ERRXTRACE("connect_i, socket failed, err=" << getLastError());
            return KMError::FAILED;
        }
    }
    setSocketOption();

    int addr_len = km_get_addr_length(ss_addr);
    int ret = ::connect(fd_, (struct sockaddr *)&ss_addr, addr_len);
    if (0 == ret) {
        setState(State::CONNECTING); // wait for writable event
    }
    else if (ret < 0 &&
#ifdef KUMA_OS_WIN
        WSAEWOULDBLOCK
#else
        EINPROGRESS
#endif
        == getLastError()) {
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

    registerFd(fd_);

    return KMError::NOERR;
}

KMError PollSocket::attachFd(SOCKET_FD fd)
{
    auto err = SocketBase::attachFd(fd);
    if (err != KMError::NOERR) {
        return err;
    }
    registerFd(fd_);
    return KMError::NOERR;
}

KMError PollSocket::detachFd(SOCKET_FD &fd)
{
    unregisterFd(fd_, false);
    return SocketBase::detachFd(fd);
}

void PollSocket::registerFd(SOCKET_FD fd)
{
    auto loop = loop_.lock();
    if (loop && fd != INVALID_FD) {
        loop->registerFd(fd, KUMA_EV_NETWORK, [this](KMEvent ev, void*, size_t) { ioReady(ev); });
        registered_ = true;
    }
}

void PollSocket::unregisterFd(SOCKET_FD fd, bool close_fd)
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

int PollSocket::send(const void* data, size_t length)
{
    auto ret = SocketBase::send(data, length);
    if (ret >= 0 && static_cast<size_t>(ret) < length) {
        notifySendBlocked();
    }
    
    return ret;
}

int PollSocket::send(iovec* iovs, int count)
{
    size_t bytes_total = 0;
    for (int i = 0; i < count; ++i) {
        bytes_total += iovs[i].iov_len;
    }
    if (bytes_total == 0) {
        return 0;
    }
    auto ret = SocketBase::send(iovs, count);
    if (ret >= 0 && static_cast<size_t>(ret) < bytes_total) {
        notifySendBlocked();
    }

    return ret;
}

KMError PollSocket::close()
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

KMError PollSocket::pause()
{
    auto loop = loop_.lock();
    if (loop) {
        return loop->updateFd(fd_, KUMA_EV_ERROR);
    }
    return KMError::INVALID_STATE;
}

KMError PollSocket::resume()
{
    auto loop = loop_.lock();
    if (loop) {
        return loop->updateFd(fd_, KUMA_EV_NETWORK);
    }
    return KMError::INVALID_STATE;
}

void PollSocket::notifySendBlocked()
{
    auto loop = loop_.lock();
    if (loop && loop->isPollLT()) {
        loop->updateFd(fd_, KUMA_EV_NETWORK);
    }
}

void PollSocket::onSend(KMError err)
{
    auto loop = loop_.lock();
    if (loop && loop->isPollLT() && fd_ != INVALID_FD) {
        loop->updateFd(fd_, KUMA_EV_READ | KUMA_EV_ERROR);
    }
    SocketBase::onSend(err);
}

void PollSocket::ioReady(KMEvent events)
{
    switch (getState())
    {
    case State::CONNECTING:
    {
        if (events & KUMA_EV_ERROR) {
            KUMA_ERRXTRACE("ioReady, KUMA_EV_ERROR on CONNECTING, events=" << events << ", err=" << getLastError());
            onConnect(KMError::POLL_ERROR);
        }
        else {
            DESTROY_DETECTOR_SETUP();
            onConnect(KMError::NOERR);
            DESTROY_DETECTOR_CHECK_VOID();
            if ((events & KUMA_EV_READ)) {
                onReceive(KMError::NOERR);
            }
        }
        break;
    }

    case State::OPEN:
    {
        if (events & KUMA_EV_READ) {// handle EPOLLIN firstly
            DESTROY_DETECTOR_SETUP();
            onReceive(KMError::NOERR);
            DESTROY_DETECTOR_CHECK_VOID();
        }
        if ((events & KUMA_EV_ERROR) && getState() == State::OPEN) {
            KUMA_ERRXTRACE("ioReady, KUMA_EV_ERROR on OPEN, events=" << events << ", err=" << getLastError());
            onClose(KMError::POLL_ERROR);
            break;
        }
        if ((events & KUMA_EV_WRITE) && getState() == State::OPEN) {
            onSend(KMError::NOERR);
        }
        break;
    }
    default:
        //KUMA_WARNXTRACE("ioReady, invalid state="<<getState()<<", events="<<events);
        break;
    }
}
