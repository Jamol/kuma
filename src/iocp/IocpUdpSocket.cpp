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
#include "IocpUdpSocket.h"
#include "util/util.h"
#include "util/kmtrace.h"

using namespace kuma;

IocpUdpSocket::IocpUdpSocket(const EventLoopPtr &loop)
: UdpSocketBase(loop), IocpBase(IocpUdpWrapper::create())
{
    KM_SetObjKey("IocpUdpSocket");
}

IocpUdpSocket::~IocpUdpSocket()
{
    cleanup();
}

bool IocpUdpSocket::registerFd(SOCKET_FD fd)
{
    return IocpBase::registerFd(loop_.lock(), fd);
}

void IocpUdpSocket::unregisterFd(SOCKET_FD fd, bool close_fd)
{
    IocpBase::unregisterFd(loop_.lock(), fd, close_fd);
}

KMError IocpUdpSocket::bind(const std::string &bind_host, uint16_t bind_port, uint32_t udp_flags)
{
    auto ret = UdpSocketBase::bind(bind_host, bind_port, udp_flags);
    if (ret != KMError::NOERR) {
        return ret;
    }
    postRecvOperation(fd_);
    return KMError::NOERR;
}

int IocpUdpSocket::receive(void *data, size_t length, char *ip, size_t ip_len, uint16_t &port)
{
    if (INVALID_FD == fd_) {
        KUMA_ERRXTRACE("receive, invalid fd");
        return -1;
    }
    if (recvPending()) {
        return 0;
    }
    
    if (!recvBuffer().empty()) {
        if (recvBuffer().size() > length) {
            return int(KMError::BUFFER_TOO_SMALL);
        }
        auto bytes_read = recvBuffer().read(data, length);
        auto ctx = (IocpUdpWrapper*)iocp_ctx_.get();
        km_get_sock_addr((struct sockaddr*)&ctx->ss_addr_, ctx->addr_len_, ip, (uint32_t)ip_len, &port);
        //KUMA_INFOXTRACE("receive, bytes_read=" << bytes_read<<", len="<<length);
        return static_cast<int>(bytes_read);
    }
    
    auto ret = UdpSocketBase::receive(data, length, ip, ip_len, port);
    if (ret == 0) {
        postRecvOperation(fd_);
    }

    //KUMA_INFOXTRACE("receive, ret="<<ret<<", len="<<length);
    return ret;
}

void IocpUdpSocket::onReceive(size_t io_size)
{
    if (io_size == 0) {
        if (fd_ != INVALID_FD) {
            KUMA_WARNXTRACE("onReceive, io_size=0");
            cleanup();
            onClose(KMError::SOCK_ERROR);
        }
        return;
    }
    UdpSocketBase::onReceive(KMError::NOERR);
}

void IocpUdpSocket::ioReady(IocpContext::Op op, size_t io_size)
{
    //KUMA_INFOXTRACE("ioReady, op="<<int(op)<<", io_size="<< io_size);
    if (op == IocpContext::Op::RECV) {
        onReceive(io_size);
    }
    else {
        KUMA_WARNXTRACE("ioReady, invalid op: "<<int(op));
    }
}
