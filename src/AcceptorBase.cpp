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

#include "kmconf.h"

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

#include <stdarg.h>
#include <errno.h>

#include "EventLoopImpl.h"
#include "AcceptorBase.h"
#include "util/util.h"
#include "util/kmtrace.h"

using namespace kuma;

AcceptorBase::AcceptorBase(const EventLoopPtr &loop)
: loop_(loop)
{
    KM_SetObjKey("AcceptorBase");
}

AcceptorBase::~AcceptorBase()
{
    if (!closed_) {
        AcceptorBase::close();
    }
}

void AcceptorBase::cleanup()
{
    if(INVALID_FD != fd_) {
        SOCKET_FD fd = fd_;
        fd_ = INVALID_FD;
        ::shutdown(fd, 2);
        unregisterFd(fd, true);
    }
}

KMError AcceptorBase::listen(const std::string &host, uint16_t port)
{
    KUMA_INFOXTRACE("startListen, host="<<host<<", port="<<port);
    if (INVALID_FD != fd_) {
        return KMError::INVALID_STATE;
    }
    sockaddr_storage ss_addr = {0};
    struct addrinfo hints = {0};
    hints.ai_family = AF_UNSPEC;
    hints.ai_flags = AI_ADDRCONFIG; // will block 10 seconds in some case if not set AI_ADDRCONFIG
    if(km_set_sock_addr(host.c_str(), port, &hints, (struct sockaddr*)&ss_addr, sizeof(ss_addr)) != 0) {
        return KMError::INVALID_PARAM;
    }
    ss_family_ = ss_addr.ss_family;
    fd_ = ::socket(ss_addr.ss_family, SOCK_STREAM, 0);
    if(INVALID_FD == fd_) {
        KUMA_ERRXTRACE("startListen, socket failed, err="<<getLastError());
        return KMError::FAILED;
    }
    setSocketOption();
    int addr_len = km_get_addr_length(ss_addr);
    int ret = ::bind(fd_, (struct sockaddr*)&ss_addr, addr_len);
    if(ret < 0) {
        KUMA_ERRXTRACE("startListen, bind failed, err="<<getLastError());
        return KMError::FAILED;
    }
    if(::listen(fd_, 128) != 0) {
        closeFd(fd_);
        fd_ = INVALID_FD;
        KUMA_ERRXTRACE("startListen, socket listen fail, err="<<getLastError());
        return KMError::FAILED;
    }
    closed_ = false;
    registerFd(fd_);
    return KMError::NOERR;
}

bool AcceptorBase::registerFd(SOCKET_FD fd)
{
    auto loop = loop_.lock();
    if (loop && fd != INVALID_FD) {
        if (loop->registerFd(fd, KUMA_EV_NETWORK, [this](KMEvent ev, void *ol, size_t sz) { ioReady(ev, ol, sz); }) == KMError::NOERR) {
            registered_ = true;
        }
    }
    return registered_;
}

void AcceptorBase::unregisterFd(SOCKET_FD fd, bool close_fd)
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
        closeFd(fd);
    }
}

void AcceptorBase::setSocketOption()
{
    if(INVALID_FD == fd_) {
        return ;
    }
    
#ifdef KUMA_OS_LINUX
    fcntl(fd_, F_SETFD, FD_CLOEXEC);
#endif
    
    // nonblock
    set_nonblocking(fd_);
    
    int opt_val = 1;
    setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR, (char*)&opt_val, sizeof(int));
}

KMError AcceptorBase::close()
{
    KUMA_INFOXTRACE("close");
    if (!closed_) {
        closed_ = true;
        auto loop = loop_.lock();
        if (loop) {
            loop->sync([this] {
                cleanup();
            });
        }
    }
    return KMError::NOERR;
}

void AcceptorBase::onAccept()
{
    SOCKET_FD fd = INVALID_FD;
    while(!closed_) {
        fd = ::accept(fd_, NULL, NULL);
        if(INVALID_FD == fd) {
            if (EINTR == errno) {
                continue;
            }
            return ;
        }
        onAccept(fd);
    }
}

void AcceptorBase::onAccept(SOCKET_FD fd)
{
    char peer_ip[128] = { 0 };
    uint16_t peer_port = 0;

    sockaddr_storage ss_addr = { 0 };
#if defined(KUMA_OS_LINUX) || defined(KUMA_OS_MAC)
    socklen_t ss_len = sizeof(ss_addr);
#else
    int ss_len = sizeof(ss_addr);
#endif
    int ret = getpeername(fd, (struct sockaddr*)&ss_addr, &ss_len);
    if (ret == 0) {
        km_get_sock_addr((struct sockaddr*)&ss_addr, sizeof(ss_addr), peer_ip, sizeof(peer_ip), &peer_port);
    }
    else {
        KUMA_WARNXTRACE("onAccept, getpeername failed, err=" << getLastError());
    }

    KUMA_INFOXTRACE("onAccept, fd=" << fd << ", peer_ip=" << peer_ip << ", peer_port=" << peer_port);
    if (!accept_cb_ || !accept_cb_(fd, peer_ip, peer_port)) {
        closeFd(fd);
    }
}

void AcceptorBase::onClose(KMError err)
{
    KUMA_INFOXTRACE("onClose, err="<<int(err));
    cleanup();
    if(error_cb_) error_cb_(err);
}

void AcceptorBase::ioReady(KMEvent events, void* ol, size_t io_size)
{
    if (events & KUMA_EV_ERROR) {
        KUMA_ERRXTRACE("ioReady, EPOLLERR or EPOLLHUP, events=" << events << ", err=" << getLastError());
        onClose(KMError::POLL_ERROR);
    }
    else {
        onAccept();
    }
}
