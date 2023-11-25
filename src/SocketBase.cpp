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

#include "SocketBase.h"
#include "libkev/src/utils/utils.h"
#include "libkev/src/utils/kmtrace.h"
#include "libkev/src/utils/skutils.h"
#include "utils/utils.h"

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

SocketBase::SocketBase(const EventLoopPtr &loop)
    : loop_(loop)
{
    KM_SetObjKey("SocketBase");
}

SocketBase::~SocketBase()
{
    if (getState() != State::CLOSED) {
        SocketBase::close();
    }
}

void SocketBase::cleanup()
{
    timer_.reset();
    if (!dns_token_.expired()) {
        DnsResolver::get().cancel("", dns_token_);
        dns_token_.reset();
    }

    if (INVALID_FD != fd_) {
        SOCKET_FD fd = fd_;
        fd_ = INVALID_FD;
        shutdown(fd, 2);
        unregisterFd(fd, true);
    }
}

SOCKET_FD SocketBase::createFd(int addr_family)
{
    return ::socket(addr_family, SOCK_STREAM, 0);
}

KMError SocketBase::bind(const std::string &bind_host, uint16_t bind_port)
{
    KM_INFOTRACE("bind, bind_host=" << bind_host << ", bind_port=" << bind_port);
    if (getState() != State::IDLE && getState() != State::CLOSED) {
        KM_ERRXTRACE("bind, invalid state, state=" << getState());
        return KMError::INVALID_STATE;
    }
    if (fd_ != INVALID_FD) {
        cleanup();
    }
    sockaddr_storage ss_addr = { 0 };
    struct addrinfo hints = { 0 };
    hints.ai_family = AF_UNSPEC;
    hints.ai_flags = AI_NUMERICHOST;//AI_ADDRCONFIG; // will block 10 seconds in some case if not set AI_ADDRCONFIG
    if (kev::km_set_sock_addr(bind_host.c_str(), bind_port, &hints, (struct sockaddr*)&ss_addr, sizeof(ss_addr)) != 0) {
        return KMError::INVALID_PARAM;
    }
    fd_ = createFd(ss_addr.ss_family);
    if (INVALID_FD == fd_) {
        KM_ERRXTRACE("bind, socket failed, err=" << kev::SKUtils::getLastError());
        return KMError::FAILED;
    }
    auto addr_len = static_cast<socklen_t>(kev::km_get_addr_length(ss_addr));
    int ret = ::bind(fd_, (struct sockaddr*)&ss_addr, addr_len);
    if (ret < 0) {
        KM_ERRXTRACE("bind, bind failed, err=" << kev::SKUtils::getLastError());
        return KMError::FAILED;
    }
    return KMError::NOERR;
}

KMError SocketBase::connect(const std::string &host, uint16_t port, EventCallback cb, uint32_t timeout_ms)
{
    KM_INFOXTRACE("connect, host=" << host << ", port=" << port);
    if (getState() != State::IDLE && getState() != State::CLOSED) {
        KM_ERRXTRACE("connect, invalid state, state=" << getState());
        return KMError::INVALID_STATE;
    }
    connect_cb_ = std::move(cb);
    if (timeout_ms > 0 && timeout_ms != uint32_t(-1)) {
        auto loop = loop_.lock();
        if (loop) {
            timer_ = std::make_unique<Timer::Impl>(loop->getTimerMgr());
            timer_->schedule(timeout_ms, kev::Timer::Mode::ONE_SHOT, [this] {
                onConnect(KMError::TIMEOUT);
            });
        }
    }
    if (!kev::km_is_ip_address(host.c_str())) {
        sockaddr_storage ss_addr = { 0 };
        if (DnsResolver::get().getAddress(host, port, ss_addr) == KMError::NOERR) {
            return connect_i(ss_addr, timeout_ms);
        }
        setState(RESOLVING);
        dns_token_ = DnsResolver::get().resolve(host, port, [this](KMError err, const sockaddr_storage &addr) {
            onResolved(err, addr);
        });
        return KMError::NOERR;
    }
    return connect_i(host, port, timeout_ms);
}

KMError SocketBase::connect_i(const std::string &host, uint16_t port, uint32_t timeout_ms)
{
    sockaddr_storage ss_addr = { 0 };
    struct addrinfo hints = { 0 };
    hints.ai_family = AF_UNSPEC;
    hints.ai_flags = AI_NUMERICHOST | AI_ADDRCONFIG; // will block 10 seconds in some case if not set AI_ADDRCONFIG
    if (kev::km_set_sock_addr(host.c_str(), port, &hints, (struct sockaddr*)&ss_addr, sizeof(ss_addr)) != 0) {
        auto err = kev::SKUtils::getLastError();
        KM_ERRXTRACE("connect_i, DNS resolving failure, host=" << host << ", err=" << err);
        return KMError::INVALID_PARAM;
    }
    return connect_i(ss_addr, timeout_ms);
}

KMError SocketBase::connect_i(const sockaddr_storage &ss_addr, uint32_t timeout_ms)
{
    if (INVALID_FD == fd_) {
        fd_ = createFd(ss_addr.ss_family);
        if (INVALID_FD == fd_) {
            KM_ERRXTRACE("connect_i, socket failed, err=" << kev::SKUtils::getLastError());
            return KMError::FAILED;
        }
    }
    setSocketOption();

    auto addr_len = static_cast<socklen_t>(kev::km_get_addr_length(ss_addr));
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
        == kev::SKUtils::getLastError()) {
        setState(State::CONNECTING);
    }
    else {
        KM_ERRXTRACE("connect_i, error, fd=" << fd_ << ", err=" << kev::SKUtils::getLastError());
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
        kev::km_get_sock_addr((struct sockaddr*)&ss_addr, sizeof(ss_addr), local_ip, sizeof(local_ip), &local_port);
    }

    KM_INFOXTRACE("connect_i, fd=" << fd_ << ", local_ip=" << local_ip
        << ", local_port=" << local_port << ", state=" << getState());

    if (!registerFd(fd_)) {
        KM_ERRXTRACE("connect_i, failed to register fd");
        cleanup();
        setState(State::CLOSED);
        return KMError::FAILED;
    }
    return KMError::NOERR;
}

KMError SocketBase::attachFd(SOCKET_FD fd)
{
    if (getState() != State::IDLE && getState() != State::CLOSED) {
        KM_ERRXTRACE("attachFd, invalid state, fd="<<fd<<", state=" << getState());
        return KMError::INVALID_STATE;
    }
    KM_INFOXTRACE("attachFd, fd=" << fd << ", state=" << getState());

    fd_ = fd;
    setSocketOption();
    setState(State::OPEN);
    if (!registerFd(fd_)) {
        KM_ERRXTRACE("attachFd, failed to register fd");
        cleanup();
        setState(State::CLOSED);
        return KMError::FAILED;
    }
    return KMError::NOERR;
}

KMError SocketBase::detachFd(SOCKET_FD &fd)
{
    KM_INFOXTRACE("detachFd, fd=" << fd_ << ", state=" << getState());
    unregisterFd(fd_, false);
    fd = fd_;
    fd_ = INVALID_FD;
    cleanup();
    setState(State::CLOSED);
    return KMError::NOERR;
}

bool SocketBase::registerFd(SOCKET_FD fd)
{
    auto loop = loop_.lock();
    if (loop && fd != INVALID_FD) {
        auto cb = [this](SOCKET_FD, KMEvent ev, void* ol, size_t io_size) {
            ioReady(ev, ol, io_size);
        };
        registered_ = true;
        if (loop->registerFd(fd, kEventNetwork, std::move(cb)) != kev::Result::OK) {
            registered_ = false;
        }
    }
    return registered_;
}

void SocketBase::unregisterFd(SOCKET_FD fd, bool close_fd)
{
    if (registered_) {
        registered_ = false;
        auto loop = loop_.lock();
        if (loop && fd != INVALID_FD) {
            loop->unregisterFd(fd, close_fd);
            return;
        }
    }
    // uregistered or loop stopped
    if (close_fd && fd != INVALID_FD) {
        kev::SKUtils::close(fd);
    }
}

int SocketBase::send(const void *data, size_t length)
{
    if (!isReady()) {
        KM_WARNXTRACE("send, invalid state=" << getState());
        return 0;
    }

    auto ret = kev::SKUtils::send(fd_, data, length, 0);
    if (0 == ret) {
        KM_WARNXTRACE("send, peer closed, err=" << kev::SKUtils::getLastError());
        ret = -1;
    }
    else if (ret < 0) {
        if (kev::SKUtils::getLastError() == EAGAIN ||
#ifdef KUMA_OS_WIN
            WSAEWOULDBLOCK
#else
            EWOULDBLOCK
#endif
            == kev::SKUtils::getLastError()) {
            ret = 0;
        }
        else {
            KM_ERRXTRACE("send, failed, err=" << kev::SKUtils::getLastError());
        }
    }

    if (ret >= 0 && static_cast<size_t>(ret) < length) {
        notifySendBlocked();
    } else if (ret < 0) {
        cleanup();
        setState(State::CLOSED);
    }
    
    //KM_INFOXTRACE("send, ret="<<ret<<", len="<<length);
    return static_cast<int>(ret);
}

int SocketBase::send(const iovec *iovs, int count)
{
    if (!isReady()) {
        KM_WARNXTRACE("send 2, invalid state=" << getState());
        return 0;
    }

    size_t bytes_total = 0;
    for (int i = 0; i < count; ++i) {
        bytes_total += iovs[i].iov_len;
    }
    if (bytes_total == 0) {
        return 0;
    }

    auto ret = kev::SKUtils::send(fd_, iovs, count);
    if (0 == ret) {
        KM_WARNXTRACE("send 2, peer closed");
        ret = -1;
    }
    else if (ret < 0) {
        if (EAGAIN == kev::SKUtils::getLastError() ||
#ifdef KUMA_OS_WIN
            WSAEWOULDBLOCK == kev::SKUtils::getLastError() || WSA_IO_PENDING
#else
            EWOULDBLOCK
#endif
            == kev::SKUtils::getLastError()) {
            ret = 0;
        }
        else {
            KM_ERRXTRACE("send 2, fail, err=" << kev::SKUtils::getLastError());
        }
    }

    if (ret >= 0 && static_cast<size_t>(ret) < bytes_total) {
        notifySendBlocked();
    } else if (ret < 0) {
        cleanup();
        setState(State::CLOSED);
    }

    //KM_INFOXTRACE("send, ret="<<ret);
    return static_cast<int>(ret);
}

int SocketBase::send(const KMBuffer &buf)
{
    iovec iovs[128] = { {0} };
    int count = 0;
    for (auto it = buf.begin(); it != buf.end(); ++it) {
        if (it->length() > 0) {
            if (count < 128) {
                iovs[count].iov_base = static_cast<char*>(it->readPtr());
                iovs[count++].iov_len = static_cast<decltype(iovs[0].iov_len)>(it->length());
            } else {
                break; // send partial data
            }
        }
    }
    
    if (count <= 0) {
        return 0;
    }
    return send(iovs, count);
}

int SocketBase::receive(void *data, size_t length)
{
    if (!isReady()) {
        return 0;
    }
    
    auto ret = kev::SKUtils::recv(fd_, data, length, 0);
    if (0 == ret) {
        KM_WARNXTRACE("receive, peer closed, err=" << kev::SKUtils::getLastError());
        ret = -1;
    }
    else if (ret < 0) {
        if (EAGAIN == kev::SKUtils::getLastError() ||
#ifdef WIN32
            WSAEWOULDBLOCK
#else
            EWOULDBLOCK
#endif
            == kev::SKUtils::getLastError()) {
            ret = 0;
        }
        else {
            KM_ERRXTRACE("receive, failed, err=" << kev::SKUtils::getLastError());
        }
    }

    if (ret < 0) {
        cleanup();
        setState(State::CLOSED);
    }

    //KM_INFOXTRACE("receive, ret="<<ret<<", len="<<length);
    return static_cast<int>(ret);
}

KMError SocketBase::close()
{
    KM_INFOXTRACE("close, state=" << getState());
    if (getState() != State::CLOSED) {
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
    }
    return KMError::NOERR;
}

KMError SocketBase::pause()
{
    auto loop = loop_.lock();
    if (loop && isReady()) {
        return toKMError(loop->updateFd(fd_, kEventError));
    }
    return KMError::INVALID_STATE;
}

KMError SocketBase::resume()
{
    auto loop = loop_.lock();
    if (loop && isReady()) {
        return toKMError(loop->updateFd(fd_, kEventNetwork));
    }
    return KMError::INVALID_STATE;
}

void SocketBase::setSocketOption()
{
    if (INVALID_FD == fd_) {
        return;
    }

#ifdef KUMA_OS_LINUX
    fcntl(fd_, F_SETFD, FD_CLOEXEC);
#endif

    // nonblock
    kev::set_nonblocking(fd_);

    if (0) {
        int opt_val = 1;
        setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR, (char*)&opt_val, sizeof(int));
    }

    if (kev::set_tcpnodelay(fd_) != 0) {
        KM_WARNXTRACE("setSocketOption, failed to set TCP_NODELAY, fd=" << fd_ << ", err=" << kev::SKUtils::getLastError());
    }
    
#ifdef KUMA_OS_MAC
    // ignore SIGPIPE
    int opt_val = 1;
    if (setsockopt(fd_, SOL_SOCKET, SO_NOSIGPIPE, &opt_val, sizeof(opt_val))) {
        // failed
    }
#endif
}

void SocketBase::notifySendBlocked()
{
    auto loop = loop_.lock();
    if (loop && loop->isPollLT()) {
        loop->updateFd(fd_, kEventNetwork);
    }
}

void SocketBase::notifySendReady()
{
    auto loop = loop_.lock();
    if (loop && loop->isPollLT() && fd_ != INVALID_FD) {
        loop->updateFd(fd_, kEventRead | kEventError);
    }
}

void SocketBase::onResolved(KMError err, const sockaddr_storage &addr)
{
    auto loop = loop_.lock();
    if (loop) {
        loop->async([=] { // addr is captured by value
            if (err == KMError::NOERR) {
                connect_i(addr, -1);
            }
            else {
                onConnect(err);
            }
        });
    }
}

void SocketBase::onConnect(KMError err)
{
    KM_INFOXTRACE("onConnect, err=" << int(err) << ", state=" << getState());
    timer_.reset();
    if (KMError::NOERR == err) {
        setState(State::OPEN);
    }
    else {
        cleanup();
        setState(State::CLOSED);
    }
    auto connect_cb(std::move(connect_cb_));
    if (connect_cb) connect_cb(err);
}

void SocketBase::onSend(KMError err)
{
    notifySendReady();
    if (write_cb_ && isReady()) write_cb_(err);
}

void SocketBase::onReceive(KMError err)
{
    if (read_cb_ && isReady()) read_cb_(err);
}

void SocketBase::onClose(KMError err)
{
    KM_INFOXTRACE("onClose, err=" << int(err) << ", state=" << getState());
    cleanup();
    setState(State::CLOSED);
    if (error_cb_) error_cb_(err);
}

void SocketBase::ioReady(KMEvent events, void *ol, size_t io_size)
{
    switch (getState())
    {
    case State::CONNECTING:
    {
        if (events & kEventError) {
            KM_ERRXTRACE("ioReady, kEventError on CONNECTING, events=" << events << ", err=" << kev::SKUtils::getLastError());
            onConnect(KMError::POLL_ERROR);
        }
        else {
            DESTROY_DETECTOR_SETUP();
            onConnect(KMError::NOERR);
            DESTROY_DETECTOR_CHECK_VOID();
            if ((events & kEventRead)) {
                onReceive(KMError::NOERR);
            }
        }
        break;
    }

    case State::OPEN:
    {
        if (events & kEventRead) {// handle EPOLLIN firstly
            DESTROY_DETECTOR_SETUP();
            onReceive(KMError::NOERR);
            DESTROY_DETECTOR_CHECK_VOID();
        }
        if ((events & kEventError) && getState() == State::OPEN) {
            KM_ERRXTRACE("ioReady, kEventError on OPEN, events=" << events << ", err=" << kev::SKUtils::getLastError());
            onClose(KMError::POLL_ERROR);
            break;
        }
        if ((events & kEventWrite) && getState() == State::OPEN) {
            onSend(KMError::NOERR);
        }
        break;
    }
    default:
        //KM_WARNXTRACE("ioReady, invalid state="<<getState()<<", events="<<events);
        break;
    }
}
