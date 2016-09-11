/* Copyright (c) 2014, Fengping Bao <jamol@live.com>
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
#include "TcpListenerImpl.h"
#include "util/util.h"
#include "util/kmtrace.h"

using namespace kuma;

TcpListener::Impl::Impl(EventLoop::Impl* loop)
: loop_(loop)
{
    KM_SetObjKey("TcpListener");
}

TcpListener::Impl::~Impl()
{

}

void TcpListener::Impl::cleanup()
{
    if(INVALID_FD != fd_) {
        SOCKET_FD fd = fd_;
        fd_ = INVALID_FD;
        ::shutdown(fd, 2);
        if(registered_) {
            registered_ = false;
            loop_->unregisterFd(fd, true);
        } else {
            closeFd(fd);
        }
    }
}

KMError TcpListener::Impl::startListen(const char* host, uint16_t port)
{
    KUMA_INFOXTRACE("startListen, host="<<host<<", port="<<port);
    if (INVALID_FD != fd_) {
        return KMError::INVALID_STATE;
    }
    sockaddr_storage ss_addr = {0};
    struct addrinfo hints = {0};
    hints.ai_family = AF_UNSPEC;
    hints.ai_flags = AI_ADDRCONFIG; // will block 10 seconds in some case if not set AI_ADDRCONFIG
    if(km_set_sock_addr(host, port, &hints, (struct sockaddr*)&ss_addr, sizeof(ss_addr)) != 0) {
        return KMError::INVALID_PARAM;
    }
    fd_ = ::socket(ss_addr.ss_family, SOCK_STREAM, 0);
    if(INVALID_FD == fd_) {
        KUMA_ERRXTRACE("startListen, socket failed, err="<<getLastError());
        return KMError::FAILED;
    }
    setSocketOption();
    int addr_len = sizeof(ss_addr);
#ifdef KUMA_OS_MAC
    if(AF_INET == ss_addr.ss_family)
        addr_len = sizeof(sockaddr_in);
    else
        addr_len = sizeof(sockaddr_in6);
#endif
    int ret = ::bind(fd_, (struct sockaddr*)&ss_addr, addr_len);
    if(ret < 0) {
        KUMA_ERRXTRACE("startListen, bind failed, err="<<getLastError());
        return KMError::FAILED;
    }
    if(::listen(fd_, 3000) != 0) {
        closeFd(fd_);
        fd_ = INVALID_FD;
        KUMA_ERRXTRACE("startListen, socket listen fail, err="<<getLastError());
        return KMError::FAILED;
    }
    stopped_ = false;
    loop_->registerFd(fd_, KUMA_EV_NETWORK, [this] (uint32_t ev) { ioReady(ev); });
    registered_ = true;
    return KMError::NOERR;
}

KMError TcpListener::Impl::stopListen(const char* host, uint16_t port)
{
    KUMA_INFOXTRACE("stopListen");
    stopped_ = true;
    loop_->runInEventLoopSync([this] {
        cleanup();
    });
    return KMError::NOERR;
}

void TcpListener::Impl::setSocketOption()
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

KMError TcpListener::Impl::close()
{
    KUMA_INFOXTRACE("close");
    stopped_ = true;
    loop_->runInEventLoopSync([this] {
        cleanup();
    });
    return KMError::NOERR;
}

void TcpListener::Impl::onAccept()
{
    SOCKET_FD fd = INVALID_FD;
    while(!stopped_) {
        fd = ::accept(fd_, NULL, NULL);
        if(INVALID_FD == fd) {
            if (EINTR == errno) {
                continue;
            }
            return ;
        }
        char peer_ip[128] = {0};
        uint16_t peer_port = 0;
        
        sockaddr_storage ss_addr = {0};
#if defined(KUMA_OS_LINUX) || defined(KUMA_OS_MAC)
        socklen_t ss_len = sizeof(ss_addr);
#else
        int ss_len = sizeof(ss_addr);
#endif
        int ret = getpeername(fd, (struct sockaddr*)&ss_addr, &ss_len);
        if(ret == 0) {
            km_get_sock_addr((struct sockaddr*)&ss_addr, sizeof(ss_addr), peer_ip, sizeof(peer_ip), &peer_port);
        } else {
            KUMA_WARNXTRACE("onAccept, getpeername failed, err="<<getLastError());
        }
        
        KUMA_INFOXTRACE("onAccept, fd="<<fd<<", peer_ip="<<peer_ip<<", peer_port="<<peer_port);
        if(accept_cb_) {
            accept_cb_(fd, peer_ip, peer_port);
        } else {
            closeFd(fd);
        }
    }
}

void TcpListener::Impl::onClose(KMError err)
{
    KUMA_INFOXTRACE("onClose, err="<<int(err));
    cleanup();
    if(error_cb_) error_cb_(err);
}

void TcpListener::Impl::ioReady(uint32_t events)
{
    if(events & KUMA_EV_ERROR) {
        KUMA_ERRXTRACE("ioReady, EPOLLERR or EPOLLHUP, events="<<events<<", err="<<getLastError());
        onClose(KMError::POLL_ERROR);
    } else {
        onAccept();
    }
}
