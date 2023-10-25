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
# include <cstring>
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
#include "UdpSocketBase.h"
#include "libkev/src/utils/utils.h"
#include "libkev/src/utils/kmtrace.h"
#include "libkev/src/utils/skutils.h"
#include "DnsResolver.h"

#ifdef KUMA_OS_MAC
#define IPV6_ADD_MEMBERSHIP IPV6_JOIN_GROUP
#define IPV6_DROP_MEMBERSHIP IPV6_LEAVE_GROUP
#endif

using namespace kuma;

static bool getSockAddr(const std::string &host, uint16_t port, sockaddr_storage &ss_addr);


UdpSocketBase::UdpSocketBase(const EventLoopPtr &loop)
: loop_(loop)
{
    KM_SetObjKey("UdpSocketBase");
}

UdpSocketBase::~UdpSocketBase()
{
    if (INVALID_FD != fd_) {
        UdpSocketBase::close();
    }
}

bool UdpSocketBase::initSocket(int ss_family)
{
    if (INVALID_FD != fd_) {
        return true;
    }

    fd_ = socket(ss_family, SOCK_DGRAM, 0);
    if (INVALID_FD == fd_) {
        KM_ERRXTRACE("initSocket, socket error, err=" << kev::SKUtils::getLastError());
        return false;
    }
    setSocketOption();
    registerFd(fd_);
    return true;
}

void UdpSocketBase::printSocket() const
{
    if (INVALID_FD != fd_) {
        sockaddr_storage ss_addr = {0};
#if defined(KUMA_OS_LINUX) || defined(KUMA_OS_MAC)
        socklen_t len = sizeof(ss_addr);
#else
        int len = sizeof(ss_addr);
#endif
        char local_ip[128] = {0};
        uint16_t local_port = 0;
        if(getsockname(fd_, (struct sockaddr*)&ss_addr, &len) != -1) {
            kev::km_get_sock_addr((struct sockaddr*)&ss_addr, sizeof(ss_addr), local_ip, sizeof(local_ip), &local_port);
        }
        KM_INFOXTRACE("printSocket, fd="<<fd_<<", local_ip="<<local_ip<<", local_port="<<local_port);
    }
}

void UdpSocketBase::cleanup()
{
    if (INVALID_FD != fd_) {
        SOCKET_FD fd = fd_;
        fd_ = INVALID_FD;
        shutdown(fd, 2);
        unregisterFd(fd, true);
        connected_ = false;
    }
}

KMError UdpSocketBase::bind(const std::string &bind_host, uint16_t bind_port, uint32_t udp_flags)
{
    KM_INFOXTRACE("bind, bind_host="<<bind_host<<", bind_port="<<bind_port);
    if(fd_ != INVALID_FD) {
        cleanup();
    }
    
    struct addrinfo hints = {0};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_protocol = IPPROTO_UDP;
    hints.ai_flags = AI_ADDRCONFIG; // will block 10 seconds in some case if not set AI_ADDRCONFIG
    if(kev::km_set_sock_addr(bind_host.c_str(), bind_port, &hints, (struct sockaddr*)&bind_addr_, sizeof(bind_addr_)) != 0) {
        KM_ERRXTRACE("bind, km_set_sock_addr failed");
        return KMError::INVALID_PARAM;
    }
    fd_ = socket(bind_addr_.ss_family, SOCK_DGRAM, 0);
    if(INVALID_FD == fd_) {
        KM_ERRXTRACE("bind, socket error, err="<<kev::SKUtils::getLastError());
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
    auto addr_len = static_cast<socklen_t>(kev::km_get_addr_length(bind_addr_));

    if(::bind(fd_, (struct sockaddr *)&bind_addr_, addr_len) < 0) {
        KM_ERRXTRACE("bind, bind error: "<<kev::SKUtils::getLastError());
        return KMError::FAILED;
    }
    printSocket();
    registerFd(fd_);
    onSocketInitialized();
    return KMError::NOERR;
}

KMError UdpSocketBase::connect(const std::string &host, uint16_t port)
{
    KM_INFOXTRACE("connect, host="<<host<<", port="<<port);
    sockaddr_storage ss_addr = {0};
    if (!getSockAddr(host, port, ss_addr)) {
        KM_ERRXTRACE("connect, cannot resolve host, host=" << host << ", port=" << port);
        return KMError::INVALID_PARAM;
    }
    bool registered = INVALID_FD != fd_;
    if (INVALID_FD == fd_) {
        fd_ = socket(ss_addr.ss_family, SOCK_DGRAM, 0);
        if (INVALID_FD == fd_) {
            KM_ERRXTRACE("connect, socket error, err=" << kev::SKUtils::getLastError());
            return KMError::SOCK_ERROR;
        }
    }
    
    auto addr_len = static_cast<socklen_t>(kev::km_get_addr_length(ss_addr));
    int ret = ::connect(fd_, (struct sockaddr *)&ss_addr, addr_len);
    if (ret < 0) {
        KM_ERRXTRACE("connect, error, fd=" << fd_ << ", err=" << kev::SKUtils::getLastError());
        cleanup();
        return KMError::SOCK_ERROR;
    }
    connected_ = true;
    printSocket();
    setSocketOption();
    if (!registered) {
        registerFd(fd_);
        onSocketInitialized();
    }
    return KMError::NOERR;
}

bool UdpSocketBase::registerFd(SOCKET_FD fd)
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

void UdpSocketBase::unregisterFd(SOCKET_FD fd, bool close_fd)
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

void UdpSocketBase::setSocketOption()
{
    if(INVALID_FD == fd_) {
        return ;
    }
    
#ifdef KUMA_OS_LINUX
    fcntl(fd_, F_SETFD, FD_CLOEXEC);
#endif
    
    // nonblock
    kev::set_nonblocking(fd_);
    
    int opt_val = 1;
    setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR, (char*)&opt_val, sizeof(int));
}

KMError UdpSocketBase::mcastJoin(const std::string &mcast_addr, uint16_t mcast_port)
{
    KM_INFOXTRACE("mcastJoin, mcast_addr"<<mcast_addr<<", mcast_port="<<mcast_port);
    if(!kev::km_is_mcast_address(mcast_addr.c_str())) {
        KM_ERRXTRACE("mcastJoin, invalid mcast address");
        return KMError::INVALID_PARAM;
    }
    struct addrinfo hints = {0};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_protocol = IPPROTO_UDP;
    hints.ai_flags = AI_NUMERICHOST|AI_ADDRCONFIG; // will block 10 seconds in some case if not set AI_ADDRCONFIG
    kev::km_set_sock_addr(mcast_addr.c_str(), mcast_port, &hints, (struct sockaddr*)&mcast_addr_, sizeof(mcast_addr_));
    mcast_port_ = mcast_port;
    if(bind_addr_.ss_family != mcast_addr_.ss_family) {
        KM_ERRXTRACE("mcastJoin, invalid mcast address family");
        return KMError::INVALID_PARAM;
    }
    if(INVALID_FD == fd_) {
        fd_ = socket(mcast_addr_.ss_family, SOCK_DGRAM, 0);
        if(INVALID_FD == fd_) {
            KM_ERRXTRACE("mcastJoin, socket error, err="<<kev::SKUtils::getLastError());
            return KMError::SOCK_ERROR;
        }
    }
    
    if(AF_INET == mcast_addr_.ss_family) {
        sockaddr_in *pa = (sockaddr_in *)&bind_addr_;
        if(setsockopt(fd_,IPPROTO_IP, IP_MULTICAST_IF,(char *)&pa->sin_addr, sizeof(pa->sin_addr)) < 0) {
            KM_ERRXTRACE("mcastJoin, failed to set IP_MULTICAST_IF, err"<<kev::SKUtils::getLastError());
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
            KM_ERRXTRACE("mcastJoin, failed to join in multicast group, err="<<kev::SKUtils::getLastError());
            return KMError::SOCK_ERROR;
        }
    } else if(AF_INET6 == mcast_addr_.ss_family) {
        sockaddr_in6 *pa = (sockaddr_in6 *)&bind_addr_;
        if(setsockopt(fd_,IPPROTO_IP, IPV6_MULTICAST_IF,(char *)&pa->sin6_scope_id, sizeof(pa->sin6_scope_id)) < 0) {
            KM_ERRXTRACE("mcastJoin, failed to set IPV6_MULTICAST_IF, err"<<kev::SKUtils::getLastError());
        }
        pa = (sockaddr_in6*)&mcast_addr_;
        memcpy(&mcast_req_v6_.ipv6mr_multiaddr,
               &pa->sin6_addr,
               sizeof(mcast_req_v6_.ipv6mr_multiaddr));
        mcast_req_v6_.ipv6mr_interface = 0;
        
        if (setsockopt(fd_, IPPROTO_IPV6, IPV6_ADD_MEMBERSHIP, (char*) &mcast_req_v6_, sizeof(mcast_req_v6_)) != 0) {
            KM_ERRXTRACE("mcastJoin, failed to join in multicast group, err="<<kev::SKUtils::getLastError());
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
        KM_WARNXTRACE("mcastJoin, failed to set TTL, err="<<kev::SKUtils::getLastError());
    }
    char loop = 0;
    if (setsockopt(fd_,
                   AF_INET6 == mcast_addr_.ss_family ? IPPROTO_IPV6 : IPPROTO_IP,
                   AF_INET6 == mcast_addr_.ss_family ? IPV6_MULTICAST_LOOP : IP_MULTICAST_LOOP,
                   (char*) &loop, sizeof(loop)) != 0)
    {
        KM_WARNXTRACE("mcastJoin, failed to disable loop, err="<<kev::SKUtils::getLastError());
    }
    return KMError::NOERR;
}

KMError UdpSocketBase::mcastLeave(const std::string &mcast_addr, uint16_t mcast_port)
{
    KM_INFOXTRACE("mcastLeave, mcast_addr: "<<mcast_addr<<", mcast_port: "<<mcast_port);
    if(INVALID_FD == fd_) {
        return KMError::INVALID_STATE;
    }
    if(AF_INET == mcast_addr_.ss_family) {
        if(setsockopt(fd_, IPPROTO_IP, IP_DROP_MEMBERSHIP, (char*)&mcast_req_v4_, sizeof(mcast_req_v4_)) != 0) {
            KM_INFOXTRACE("mcastLeave, failed, err"<<kev::SKUtils::getLastError());
        }
    } else if(AF_INET6 == mcast_addr_.ss_family) {
        if(setsockopt(fd_, IPPROTO_IPV6, IPV6_DROP_MEMBERSHIP, (char*)&mcast_req_v6_, sizeof(mcast_req_v6_)) != 0) {
            KM_INFOXTRACE("mcastLeave, failed, err="<<kev::SKUtils::getLastError());
        }
    }
    return KMError::NOERR;
}

int UdpSocketBase::send(const void *data, size_t length, const std::string &host, uint16_t port)
{
    kev::ssize_t ret = -1;
    if (connected_) {
        ret = kev::SKUtils::send(fd_, data, length, 0);
    } else {
        sockaddr_storage ss_addr = {0};
        if (!getSockAddr(host, port, ss_addr)) {
            KM_ERRXTRACE("send, cannot resolve host, host=" << host << ", port=" << port);
            return -1;
        }
        bool initialized = INVALID_FD != fd_;
        if (!initSocket(ss_addr.ss_family)) {
            return -1;
        }
        auto addr_len = static_cast<socklen_t>(kev::km_get_addr_length(ss_addr));
        ret = kev::SKUtils::sendto(fd_, data, length, 0, (struct sockaddr*)&ss_addr, addr_len);
        if (!initialized) {
            printSocket();
            onSocketInitialized();
        }
    }
    if(0 == ret) {
        KM_ERRXTRACE("send, peer closed, err="<<kev::SKUtils::getLastError()<<", host="<<host<<", port="<<port);
        ret = -1;
    } else if(ret < 0) {
        if(EAGAIN == kev::SKUtils::getLastError() ||
#ifdef KUMA_OS_WIN
           WSAEWOULDBLOCK
#else
           EWOULDBLOCK
#endif
           == kev::SKUtils::getLastError()) {
            ret = 0;
        } else {
            KM_ERRXTRACE("send, failed, err: "<<kev::SKUtils::getLastError()<<", host="<<host<<", port="<<port);
        }
    }
    
    if (ret >= 0 && static_cast<size_t>(ret) < length) {
        notifySendBlocked();
    } else if(ret < 0) {
        //cleanup();
    }
    //KM_INFOXTRACE("send, ret="<<ret<<", len="<<length);
    return static_cast<int>(ret);
}

int UdpSocketBase::send(const iovec *iovs, int count, const std::string &host, uint16_t port)
{
    size_t bytes_total = 0;
    for (int i = 0; i < count; ++i) {
        bytes_total += iovs[i].iov_len;
    }
    if (bytes_total == 0) {
        return 0;
    }
    
    kev::ssize_t ret = -1;
    if (connected_) {
        ret = kev::SKUtils::send(fd_, iovs, count);
    } else {
        sockaddr_storage ss_addr = {0};
        if (!getSockAddr(host, port, ss_addr)) {
            KM_ERRXTRACE("send, cannot resolve host, host=" << host << ", port=" << port);
            return -1;
        }
        bool initialized = INVALID_FD != fd_;
        if (!initSocket(ss_addr.ss_family)) {
            return -1;
        }
        auto addr_len = static_cast<socklen_t>(kev::km_get_addr_length(ss_addr));
        ret = kev::SKUtils::sendto(fd_, iovs, count, 0, (const sockaddr *)&ss_addr, addr_len);
        if (!initialized) {
            printSocket();
            onSocketInitialized();
        }
    }
    if(0 == ret) {
        KM_ERRXTRACE("send, peer closed, err: "<<kev::SKUtils::getLastError()<<", host="<<host<<", port="<<port);
        ret = -1;
    } else if(ret < 0) {
        if(EAGAIN == kev::SKUtils::getLastError() ||
#ifdef WIN32
           WSAEWOULDBLOCK == kev::SKUtils::getLastError() || WSA_IO_PENDING
#else
           EWOULDBLOCK
#endif
           == kev::SKUtils::getLastError()) {
            ret = 0;
        } else {
            KM_ERRXTRACE("sendto 2, failed, err="<<kev::SKUtils::getLastError());
        }
    }
    
    if (ret >= 0 && static_cast<size_t>(ret) < bytes_total) {
        notifySendBlocked();
    } else if(ret < 0) {
        //cleanup();
    }
    //KM_INFOXTRACE("send, ret=" << ret);
    return static_cast<int>(ret);
}

int UdpSocketBase::send(const KMBuffer &buf, const std::string &host, uint16_t port)
{
    iovec iovs[128] = { {0} };
    int count = 0;
    for (auto it = buf.begin(); it != buf.end(); ++it) {
        if (it->length() > 0) {
            if (count < 128) {
                iovs[count].iov_base = static_cast<char*>(it->readPtr());
                iovs[count++].iov_len = static_cast<decltype(iovs[0].iov_len)>(it->length());
            } else {
                return 0; // buffer too long
            }
        }
    }

    if (count <= 0) {
        return 0;
    }
    return send(iovs, count, host, port);
}

int UdpSocketBase::receive(void *data, size_t length, char *ip, size_t ip_len, uint16_t &port)
{
    if(INVALID_FD == fd_) {
        KM_ERRXTRACE("receive, invalid fd");
        return -1;
    }

    sockaddr_storage ss_addr = {0};
    int ret = 0;
    if (connected_) {
        ret = (int)kev::SKUtils::recv(fd_, data, length, 0);
    } else {
        socklen_t addr_len = sizeof(ss_addr);
        ret = (int)kev::SKUtils::recvfrom(fd_, data, length, 0, (struct sockaddr*)&ss_addr, &addr_len);
    }
    if(0 == ret) {
        KM_ERRXTRACE("recv, peer closed, err"<<kev::SKUtils::getLastError());
        ret = -1;
    } else if(ret < 0) {
        if(EAGAIN == kev::SKUtils::getLastError() ||
#ifdef WIN32
           WSAEWOULDBLOCK
#else
           EWOULDBLOCK
#endif
           == kev::SKUtils::getLastError()) {
            ret = 0;
        } else {
            KM_ERRXTRACE("recv, failed, err="<<kev::SKUtils::getLastError());
        }
    } else if (!connected_ && ip && ip_len > 0) {
        kev::km_get_sock_addr((struct sockaddr*)&ss_addr, sizeof(ss_addr), ip, (uint32_t)ip_len, &port);
    }
    
    if(ret < 0) {
        //cleanup();
    }
    //KM_INFOXTRACE("receive, ret=" << ret << ", len=" << length);
    return static_cast<int>(ret);
}

KMError UdpSocketBase::close()
{
    KM_INFOXTRACE("close");
    if (fd_ != INVALID_FD) {
        auto loop = loop_.lock();
        if (loop && !loop->stopped()) {
            loop->sync([this] {
                cleanup();
            });
        }
        else {
            cleanup();
        }
    }
    return KMError::NOERR;
}

void UdpSocketBase::onSend(KMError err)
{
    notifySendReady();
}

void UdpSocketBase::onReceive(KMError err)
{
    if(read_cb_ && fd_ != INVALID_FD) read_cb_(err);
}

void UdpSocketBase::onClose(KMError err)
{
    KM_INFOXTRACE("onClose, err="<<int(err));
    cleanup();
    if(error_cb_) error_cb_(err);
}

void UdpSocketBase::notifySendBlocked()
{
    auto loop = loop_.lock();
    if (loop && loop->isPollLT()) {
        loop->updateFd(fd_, kEventNetwork);
    }
}

void UdpSocketBase::notifySendReady()
{
    auto loop = loop_.lock();
    if (loop && loop->isPollLT()) {
        loop->updateFd(fd_, kEventRead | kEventError);
    }
}

void UdpSocketBase::ioReady(KMEvent events, void *ol, size_t io_size)
{
    DESTROY_DETECTOR_SETUP();
    if (events & kEventRead) {// handle EPOLLIN firstly
        onReceive(KMError::NOERR);
    }
    DESTROY_DETECTOR_CHECK_VOID();
    if ((events & kEventError) && fd_ != INVALID_FD) {
        KM_ERRXTRACE("ioReady, EPOLLERR or EPOLLHUP, events=" << events
            << ", err=" << kev::SKUtils::getLastError());
        onClose(KMError::POLL_ERROR);
        return;
    }
    if ((events & kEventWrite) && fd_ != INVALID_FD) {
        onSend(KMError::NOERR);
    }
}

static bool getSockAddr(const std::string &host, uint16_t port, sockaddr_storage &ss_addr)
{
    if (!kev::km_is_ip_address(host.c_str())) {
        if (DnsResolver::get().resolve(host, port, ss_addr) != KMError::NOERR) {
            KM_ERRTRACE("UdpSocket::getSockAddr, cannot resolve host, host=" << host << ", port=" << port);
            return false;
        }
    }
    else {
        struct addrinfo hints = { 0 };
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_DGRAM;
        hints.ai_protocol = IPPROTO_UDP;
        hints.ai_flags = AI_NUMERICHOST | AI_ADDRCONFIG; // will block 10 seconds in some case if not set AI_ADDRCONFIG
        if (kev::km_set_sock_addr(host.c_str(), port, &hints, (struct sockaddr*)&ss_addr, sizeof(ss_addr)) != 0) {
            KM_ERRTRACE("UdpSocket::getSockAddr, cannot resolve host 2, host=" << host << ", port=" << port);
            return false;
        }
    }

    return true;
}
