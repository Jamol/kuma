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
#include "UdpSocketImpl.h"
#include "util/util.h"
#include "util/kmtrace.h"

#ifdef KUMA_OS_MAC
#define IPV6_ADD_MEMBERSHIP IPV6_JOIN_GROUP
#define IPV6_DROP_MEMBERSHIP IPV6_LEAVE_GROUP
#endif

using namespace kuma;

UdpSocket::Impl::Impl(EventLoop::Impl* loop)
: loop_(loop)
{
    KM_SetObjKey("UdpSocket");
}

UdpSocket::Impl::~Impl()
{
    cleanup();
}

void UdpSocket::Impl::cleanup()
{
    if(INVALID_FD != fd_) {
        SOCKET_FD fd = fd_;
        fd_ = INVALID_FD;
        shutdown(fd, 2);
        if(registered_) {
            registered_ = false;
            loop_->unregisterFd(fd, true);
        } else {
            closeFd(fd);
        }
    }
}

KMError UdpSocket::Impl::bind(const char *bind_host, uint16_t bind_port, uint32_t udp_flags)
{
    KUMA_INFOXTRACE("bind, bind_host="<<bind_host<<", bind_port="<<bind_port);
    if(fd_ != INVALID_FD) {
        cleanup();
    }
    
    struct addrinfo hints = {0};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_flags = AI_ADDRCONFIG; // will block 10 seconds in some case if not set AI_ADDRCONFIG
    if(km_set_sock_addr(bind_host, bind_port, &hints, (struct sockaddr*)&bind_addr_, sizeof(bind_addr_)) != 0) {
        KUMA_ERRXTRACE("bind, km_set_sock_addr failed");
        return KMError::INVALID_PARAM;
    }
    fd_ = socket(bind_addr_.ss_family, SOCK_DGRAM, 0);
    if(INVALID_FD == fd_) {
        KUMA_ERRXTRACE("bind, socket error, err="<<getLastError());
        return KMError::FAILED;
    }
    setSocketOption();

    if(AF_INET == bind_addr_.ss_family) {
        struct sockaddr_in *sa = (struct sockaddr_in*)&bind_addr_;
#if defined(KUMA_OS_LINUX) || defined(KUMA_OS_MAC)
        if(udp_flags & UDP_FLAG_MULTICAST) {
            sa->sin_addr.s_addr = htonl(INADDR_ANY);
        }
#endif
    } else if(AF_INET6 == bind_addr_.ss_family) {
        struct sockaddr_in6 *sa = (struct sockaddr_in6*)&bind_addr_;
#if defined(KUMA_OS_LINUX) || defined(KUMA_OS_MAC)
        if(udp_flags & UDP_FLAG_MULTICAST) {
            sa->sin6_addr = in6addr_any;
        }
#endif
    } else {
        return KMError::INVALID_PROTO;
    }
    int addr_len = sizeof(bind_addr_);
#ifdef KUMA_OS_MAC
    if(AF_INET == bind_addr_.ss_family)
        addr_len = sizeof(sockaddr_in);
    else
        addr_len = sizeof(sockaddr_in6);
#endif

    if(::bind(fd_, (struct sockaddr *)&bind_addr_, addr_len) < 0) {
        KUMA_ERRXTRACE("bind, bind error: "<<getLastError());
        return KMError::FAILED;
    }
    
    sockaddr_storage ss_addr = {0};
#if defined(KUMA_OS_LINUX) || defined(KUMA_OS_MAC)
    socklen_t len = sizeof(ss_addr);
#else
    int len = sizeof(ss_addr);
#endif
    char local_ip[128] = {0};
    uint16_t local_port = 0;
    if(getsockname(fd_, (struct sockaddr*)&ss_addr, &len) != -1) {
        km_get_sock_addr((struct sockaddr*)&ss_addr, sizeof(ss_addr), local_ip, sizeof(local_ip), &local_port);
    }
    KUMA_INFOXTRACE("bind, fd="<<fd_<<", local_ip="<<local_ip<<", local_port="<<local_port);
    
    loop_->registerFd(fd_, KUMA_EV_NETWORK, [this] (uint32_t ev) { ioReady(ev); });
    registered_ = true;
    return KMError::NOERR;
}

void UdpSocket::Impl::setSocketOption()
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

KMError UdpSocket::Impl::mcastJoin(const char* mcast_addr, uint16_t mcast_port)
{
    KUMA_INFOXTRACE("mcastJoin, mcast_addr"<<(mcast_addr?mcast_addr:"")<<", mcast_port="<<mcast_port);
    if(!km_is_mcast_address(mcast_addr)) {
        KUMA_ERRXTRACE("mcastJoin, invalid mcast address");
        return KMError::INVALID_PARAM;
    }
    struct addrinfo hints = {0};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_flags = AI_NUMERICHOST|AI_ADDRCONFIG; // will block 10 seconds in some case if not set AI_ADDRCONFIG
    km_set_sock_addr(mcast_addr, mcast_port, &hints, (struct sockaddr*)&mcast_addr_, sizeof(mcast_addr_));
    mcast_port_ = mcast_port;
    if(bind_addr_.ss_family != mcast_addr_.ss_family) {
        KUMA_ERRXTRACE("mcastJoin, invalid mcast address family");
        return KMError::INVALID_PARAM;
    }
    if(INVALID_FD == fd_) {
        fd_ = socket(mcast_addr_.ss_family, SOCK_DGRAM, 0);
        if(INVALID_FD == fd_) {
            KUMA_ERRXTRACE("mcastJoin, socket error, err="<<getLastError());
            return KMError::SOCK_ERROR;
        }
    }
    
    if(AF_INET == mcast_addr_.ss_family) {
        sockaddr_in *pa = (sockaddr_in *)&bind_addr_;
        if(setsockopt(fd_,IPPROTO_IP, IP_MULTICAST_IF,(char *)&pa->sin_addr, sizeof(pa->sin_addr)) < 0) {
            KUMA_ERRXTRACE("mcastJoin, failed to set IP_MULTICAST_IF, err"<<getLastError());
        }
        
        //memcpy(&mcast_req_v4_.imr_interface,
        //	&pa->sin_addr,
        //	sizeof(mcast_req_v4_.imr_interface));
        pa = (sockaddr_in*)&mcast_addr_;
        memcpy(&mcast_req_v4_.imr_multiaddr,
               &pa->sin_addr,
               sizeof(mcast_req_v4_.imr_multiaddr));
        mcast_req_v4_.imr_interface.s_addr = htonl(INADDR_ANY);
        
        if (setsockopt(fd_, IPPROTO_IP, IP_ADD_MEMBERSHIP, (char*) &mcast_req_v4_, sizeof(mcast_req_v4_)) != 0) {
            KUMA_ERRXTRACE("mcastJoin, failed to join in multicast group, err="<<getLastError());
            return KMError::SOCK_ERROR;
        }
    } else if(AF_INET6 == mcast_addr_.ss_family) {
        sockaddr_in6 *pa = (sockaddr_in6 *)&bind_addr_;
        if(setsockopt(fd_,IPPROTO_IP, IPV6_MULTICAST_IF,(char *)&pa->sin6_scope_id, sizeof(pa->sin6_scope_id)) < 0) {
            KUMA_ERRXTRACE("mcastJoin, failed to set IPV6_MULTICAST_IF, err"<<getLastError());
        }
        pa = (sockaddr_in6*)&mcast_addr_;
        memcpy(&mcast_req_v6_.ipv6mr_multiaddr,
               &pa->sin6_addr,
               sizeof(mcast_req_v6_.ipv6mr_multiaddr));
        mcast_req_v6_.ipv6mr_interface = 0;
        
        if (setsockopt(fd_, IPPROTO_IPV6, IPV6_ADD_MEMBERSHIP, (char*) &mcast_req_v6_, sizeof(mcast_req_v6_)) != 0) {
            KUMA_ERRXTRACE("mcastJoin, failed to join in multicast group, err="<<getLastError());
            return KMError::SOCK_ERROR;
        }
    } else {
        return KMError::INVALID_PARAM;
    }
    char ttl = 32;
    if (setsockopt(fd_,
                   AF_INET6 == mcast_addr_.ss_family ? IPPROTO_IPV6 : IPPROTO_IP,
                   AF_INET6 == mcast_addr_.ss_family ? IPV6_MULTICAST_HOPS : IP_MULTICAST_TTL,
                   (char*) &ttl, sizeof(ttl)) != 0)
    {
        KUMA_WARNXTRACE("mcastJoin, failed to set TTL, err="<<getLastError());
    }
    char loop = 0;
    if (setsockopt(fd_,
                   AF_INET6 == mcast_addr_.ss_family ? IPPROTO_IPV6 : IPPROTO_IP,
                   AF_INET6 == mcast_addr_.ss_family ? IPV6_MULTICAST_LOOP : IP_MULTICAST_LOOP,
                   (char*) &loop, sizeof(loop)) != 0)
    {
        KUMA_WARNXTRACE("mcastJoin, failed to disable loop, err="<<getLastError());
    }
    return KMError::NOERR;
}

KMError UdpSocket::Impl::mcastLeave(const char* mcast_addr, uint16_t mcast_port)
{
    KUMA_INFOXTRACE("mcastLeave, mcast_addr: "<<(mcast_addr?mcast_addr:"")<<", mcast_port: "<<mcast_port);
    if(INVALID_FD == fd_) {
        return KMError::INVALID_STATE;
    }
    if(AF_INET == mcast_addr_.ss_family) {
        if(setsockopt(fd_, IPPROTO_IP, IP_DROP_MEMBERSHIP, (char*)&mcast_req_v4_, sizeof(mcast_req_v4_)) != 0) {
            KUMA_INFOXTRACE("mcastLeave, failed, err"<<getLastError());
        }
    } else if(AF_INET6 == mcast_addr_.ss_family) {
        if(setsockopt(fd_, IPPROTO_IPV6, IPV6_DROP_MEMBERSHIP, (char*)&mcast_req_v6_, sizeof(mcast_req_v6_)) != 0) {
            KUMA_INFOXTRACE("mcastLeave, failed, err="<<getLastError());
        }
    }
    return KMError::NOERR;
}

int UdpSocket::Impl::send(const void* data, size_t length, const char* host, uint16_t port)
{
    if(INVALID_FD == fd_) {
        KUMA_ERRXTRACE("send, invalid fd");
        return -1;
    }
    
    sockaddr_storage ss_addr = {0};
    struct addrinfo hints = {0};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_flags = AI_NUMERICHOST|AI_ADDRCONFIG; // will block 10 seconds in some case if not set AI_ADDRCONFIG
    km_set_sock_addr(host, port, &hints, (struct sockaddr*)&ss_addr, sizeof(ss_addr));
    int addr_len = sizeof(ss_addr);
#ifdef KUMA_OS_MAC
    if(AF_INET == ss_addr.ss_family)
        addr_len = sizeof(sockaddr_in);
    else
        addr_len = sizeof(sockaddr_in6);
#endif
    int ret = (int)::sendto(fd_, (const char*)data, length, 0, (struct sockaddr*)&ss_addr, addr_len);
    if(0 == ret) {
        KUMA_ERRXTRACE("send, peer closed, err="<<getLastError()<<", host="<<host<<", port="<<port);
        ret = -1;
    } else if(ret < 0) {
        if(EAGAIN == getLastError() ||
#ifdef KUMA_OS_WIN
           WSAEWOULDBLOCK
#else
           EWOULDBLOCK
#endif
           == getLastError()) {
            ret = 0;
        } else {
            KUMA_ERRXTRACE("send, failed, err: "<<getLastError()<<", host="<<host<<", port="<<port);
        }
    }
    
    if(ret < 0) {
        //cleanup();
    } else if(ret < length) {
        if(loop_->isPollLT()) {
            loop_->updateFd(fd_, KUMA_EV_NETWORK);
        }
    }
    //WTP_INFOXTRACE("send, ret="<<ret<<", len="<<len);
    return ret;
}

int UdpSocket::Impl::send(iovec* iovs, int count, const char* host, uint16_t port)
{
    if(INVALID_FD == fd_) {
        KUMA_ERRXTRACE("send 2, invalid fd");
        return -1;
    }
    
    int ret = 0;
    if(0 == count) return 0;
    
    sockaddr_storage ss_addr = {0};
    struct addrinfo hints = {0};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_flags = AI_NUMERICHOST|AI_ADDRCONFIG; // will block 10 seconds in some case if not set AI_ADDRCONFIG
    km_set_sock_addr(host, port, &hints, (struct sockaddr*)&ss_addr, sizeof(ss_addr));
#ifdef KUMA_OS_WIN
    DWORD bytes_sent = 0;
    ret = ::WSASendTo(fd_, (LPWSABUF)iovs, count, &bytes_sent, 0,
                      (const sockaddr *)&ss_addr, sizeof(ss_addr), NULL, NULL);
    if(0 == ret) ret = (int)bytes_sent;
#else // KUMA_OS_WIN
    msghdr send_msg;
    send_msg.msg_iov = (iovec *)&iovs[0];
    send_msg.msg_iovlen = count;
    send_msg.msg_name = (struct sockaddr *)&ss_addr;
    send_msg.msg_namelen = sizeof(ss_addr);
#ifdef KUMA_OS_MAC
    if(AF_INET == ss_addr.ss_family)
        send_msg.msg_namelen = sizeof(sockaddr_in);
    else
        send_msg.msg_namelen = sizeof(sockaddr_in6);
#endif
#if defined(SOLARIS)
    send_msg.msg_accrights = 0;
    send_msg.msg_accrightslen = 0;
#else
    send_msg.msg_control = 0;
    send_msg.msg_controllen = 0;
    send_msg.msg_flags = 0;
#endif
    ret = (int)::sendmsg(fd_, &send_msg, 0);
#endif // _WIN32
    if(0 == ret) {
        KUMA_ERRXTRACE("send, peer closed, err: "<<getLastError()<<", host="<<host<<", port="<<port);
        ret = -1;
    } else if(ret < 0) {
        if(EAGAIN == getLastError() ||
#ifdef WIN32
           WSAEWOULDBLOCK == getLastError() || WSA_IO_PENDING
#else
           EWOULDBLOCK
#endif
           == getLastError()) {
            ret = 0;
        } else {
            KUMA_ERRXTRACE("sendto 2, failed, err="<<getLastError());
        }
    }
    
    if(ret < 0) {
        //cleanup();
    } else if(0 == ret) {
        if(loop_->isPollLT()) {
            loop_->updateFd(fd_, KUMA_EV_NETWORK);
        }
    }
    
    return ret;
}

int UdpSocket::Impl::receive(void* data, size_t length, char* ip, size_t ip_len, uint16_t& port)
{
    if(INVALID_FD == fd_) {
        KUMA_ERRXTRACE("receive, invalid fd");
        return -1;
    }
    
    int ret = 0;
    sockaddr_storage ss_addr = {0};
#if defined(KUMA_OS_LINUX) || defined(KUMA_OS_MAC)
    socklen_t addr_len = sizeof(ss_addr);
#else
    int addr_len = sizeof(ss_addr);
#endif
    ret = (int)::recvfrom(fd_, (char*)data, length, 0, (struct sockaddr*)&ss_addr, &addr_len);
    if(0 == ret) {
        KUMA_ERRXTRACE("recv, peer closed, err"<<getLastError());
        ret = -1;
    } else if(ret < 0) {
        if(EAGAIN == getLastError() ||
#ifdef WIN32
           WSAEWOULDBLOCK
#else
           EWOULDBLOCK
#endif
           == getLastError()) {
            ret = 0;
        } else {
            KUMA_ERRXTRACE("recv, failed, err="<<getLastError());
        }
    } else {
        km_get_sock_addr((struct sockaddr*)&ss_addr, sizeof(ss_addr), ip, (uint32_t)ip_len, &port);
    }
    
    if(ret < 0) {
        //cleanup();
    }
    return ret;
}

KMError UdpSocket::Impl::close()
{
    KUMA_INFOXTRACE("close");
    loop_->sync([this] {
        cleanup();
    });
    return KMError::NOERR;
}

void UdpSocket::Impl::onSend(KMError err)
{
    if(loop_->isPollLT()) {
        loop_->updateFd(fd_, KUMA_EV_READ | KUMA_EV_ERROR);
    }
}

void UdpSocket::Impl::onReceive(KMError err)
{
    if(read_cb_ && fd_ != INVALID_FD) read_cb_(err);
}

void UdpSocket::Impl::onClose(KMError err)
{
    KUMA_INFOXTRACE("onClose, err="<<int(err));
    cleanup();
    if(error_cb_) error_cb_(err);
}

void UdpSocket::Impl::ioReady(uint32_t events)
{
    DESTROY_DETECTOR_SETUP();
    if(events & KUMA_EV_READ) {// handle EPOLLIN firstly
        onReceive(KMError::NOERR);
    }
    DESTROY_DETECTOR_CHECK_VOID();
    if((events & KUMA_EV_ERROR) && fd_ != INVALID_FD) {
        KUMA_ERRXTRACE("ioReady, EPOLLERR or EPOLLHUP, events="<<events
                       <<", err="<<getLastError());
        onClose(KMError::POLL_ERROR);
        return;
    }
    if((events & KUMA_EV_WRITE) && fd_ != INVALID_FD) {
        onSend(KMError::NOERR);
    }
}
