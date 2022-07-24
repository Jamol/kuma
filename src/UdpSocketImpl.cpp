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
#include "libkev/src/utils/utils.h"
#include "libkev/src/utils/kmtrace.h"
#ifdef KUMA_OS_WIN
# include "iocp/IocpUdpSocket.h"
#endif

using namespace kuma;

UdpSocket::Impl::Impl(const EventLoopPtr &loop)
{
#ifdef KUMA_OS_WIN
    if (loop->getPollType() == PollType::IOCP) {
        socket_.reset(new IocpUdpSocket(loop));
    }
    else
#endif
    {
        socket_.reset(new UdpSocketBase(loop));
    }
}

UdpSocket::Impl::~Impl()
{
    if (socket_) {
        socket_->close();
    }
}

KMError UdpSocket::Impl::bind(const std::string &bind_host, uint16_t bind_port, uint32_t udp_flags)
{
    return socket_->bind(bind_host, bind_port, udp_flags);
}

KMError UdpSocket::Impl::connect(const std::string &host, uint16_t port)
{
    return socket_->connect(host, port);
}

KMError UdpSocket::Impl::mcastJoin(const std::string &mcast_addr, uint16_t mcast_port)
{
    return socket_->mcastJoin(mcast_addr, mcast_port);
}

KMError UdpSocket::Impl::mcastLeave(const std::string &mcast_addr, uint16_t mcast_port)
{
    return socket_->mcastLeave(mcast_addr, mcast_port);
}

int UdpSocket::Impl::send(const void *data, size_t length, const std::string &host, uint16_t port)
{
    return socket_->send(data, length, host, port);
}

int UdpSocket::Impl::send(const iovec *iovs, int count, const std::string &host, uint16_t port)
{
    return socket_->send(iovs, count, host, port);
}

int UdpSocket::Impl::send(const KMBuffer &buf, const std::string &host, uint16_t port)
{
    return socket_->send(buf, host, port);
}

int UdpSocket::Impl::receive(void *data, size_t length, char *ip, size_t ip_len, uint16_t &port)
{
    return socket_->receive(data, length, ip, ip_len, port);
}

KMError UdpSocket::Impl::close()
{
    return socket_->close();
}

void UdpSocket::Impl::setReadCallback(EventCallback cb)
{
    return socket_->setReadCallback(std::move(cb));
}

void UdpSocket::Impl::setErrorCallback(EventCallback cb)
{
    return socket_->setErrorCallback(std::move(cb));
}
