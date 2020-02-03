//
//  skutils.h
//  kuma
//
//  Created by Fengping Bao <jamol@live.com> on 2/2/20.
//  Copyright © 2014-2020 kuma. All rights reserved.
//

#pragma once

#include "evdefs.h"

KUMA_NS_BEGIN


#ifdef KUMA_OS_WIN
# define SK_CONST_BUF_LEN   static_cast<const char*>(buf),static_cast<int>(len)
# define SK_BUF_LEN         static_cast<char*>(buf),static_cast<int>(len)
using ssize_t = std::make_signed_t<size_t>;
#else
# define SK_CONST_BUF_LEN   buf,len
# define SK_BUF_LEN         buf,len
#endif

class SKUtils
{
public:
    static ssize_t send(SOCKET_FD fd, const void *buf, size_t len, int flags)
    {
        ssize_t ret = 0;
        do {
            ret = ::send(fd, SK_CONST_BUF_LEN, flags);
        } while(ret < 0 && getLastError() == EINTR);
        return ret;
    }
    
    static ssize_t recv(SOCKET_FD fd, void *buf, size_t len, int flags)
    {
        ssize_t ret = 0;
        do {
            ret = ::recv(fd, SK_BUF_LEN, flags);
        } while(ret < 0 && getLastError() == EINTR);
        return ret;
    }
    
    static ssize_t send(SOCKET_FD fd, const iovec *iovs, int count)
    {
        ssize_t ret = 0;
#ifdef KUMA_OS_WIN
        DWORD bytes_sent = 0;
        ret = ::WSASend(fd, (LPWSABUF)iovs, count, &bytes_sent, 0, NULL, NULL);
        if (0 == ret) ret = bytes_sent;
#else
        do {
            ret = ::writev(fd, iovs, count);
        } while(ret < 0 && getLastError() == EINTR);
#endif
        return ret;
    }
    
    static ssize_t recv(SOCKET_FD fd, const iovec *iovs, int count)
    {
        ssize_t ret = 0;
#ifdef KUMA_OS_WIN
        DWORD bytes_recv = 0;
        ret = ::WSARecv(fd, (LPWSABUF)iovs, count, &bytes_recv, 0, NULL, NULL);
        if (0 == ret) ret = (ssize_t)bytes_recv;
#else
        do {
            ret = ::readv(fd, iovs, count);
        } while(ret < 0 && getLastError() == EINTR);
#endif
        return ret;
    }
    
    static ssize_t sendto(SOCKET_FD fd, const void *buf, size_t len, int flags,
                          const sockaddr *addr, socklen_t addr_len)
    {
        ssize_t ret = 0;
        do {
            ret = ::sendto(fd, SK_CONST_BUF_LEN, 0, addr, addr_len);
        } while(ret < 0 && getLastError() == EINTR);
        return ret;
    }
    
    static ssize_t recvfrom(SOCKET_FD fd, void *buf, size_t len, int flags,
                     struct sockaddr *addr, socklen_t *addr_len)
    {
        ssize_t ret = 0;
        do {
            ret = ::recvfrom(fd, SK_BUF_LEN, 0, addr, addr_len);
        } while(ret < 0 && getLastError() == EINTR);
        return ret;
    }
    
    static ssize_t sendto(SOCKET_FD fd, const iovec *iovs, int count, int flags,
                          const sockaddr *addr, socklen_t addr_len)
    {
        ssize_t ret = 0;
#ifdef KUMA_OS_WIN
        DWORD bytes_sent = 0;
        ret = ::WSASendTo(fd, (LPWSABUF)iovs, count, &bytes_sent, 0,
                          addr, addr_len, NULL, NULL);
        if(0 == ret) ret = (ssize_t)bytes_sent;
#else // KUMA_OS_WIN
        msghdr send_msg;
        send_msg.msg_iov = const_cast<struct iovec*>(iovs);
        send_msg.msg_iovlen = count;
        send_msg.msg_name = const_cast<sockaddr*>(addr);
        send_msg.msg_namelen = addr_len;
        send_msg.msg_control = 0;
        send_msg.msg_controllen = 0;
        send_msg.msg_flags = 0;
        do {
            ret = ::sendmsg(fd, &send_msg, 0);
        } while(ret < 0 && getLastError() == EINTR);
#endif // KUMA_OS_WIN
        return ret;
    }
    
    static ssize_t recvfrom(SOCKET_FD fd, const iovec *iovs, int count, int flags,
                     struct sockaddr *addr, socklen_t *addr_len)
    {
        ssize_t ret = 0;
#ifdef KUMA_OS_WIN
        DWORD bytes_recv = 0;
        int alen = *addr_len;
        ret = ::WSARecvFrom(fd, (LPWSABUF)iovs, count, &bytes_recv, 0,
                          addr, &alen, NULL, NULL);
        *addr_len = alen;
        if(0 == ret) ret = (ssize_t)bytes_recv;
#else // KUMA_OS_WIN
        uint8_t msg_ctrl[1024];
        msghdr recv_msg;
        recv_msg.msg_iov = const_cast<struct iovec*>(iovs);
        recv_msg.msg_iovlen = count;
        recv_msg.msg_name = addr;
        recv_msg.msg_namelen = *addr_len;
        recv_msg.msg_control = msg_ctrl;
        recv_msg.msg_controllen = sizeof(msg_ctrl);
        recv_msg.msg_flags = 0;
        do {
            ret = ::recvmsg(fd, &recv_msg, 0);
        } while(ret < 0 && getLastError() == EINTR);
#endif // KUMA_OS_WIN
        return ret;
    }
};

KUMA_NS_END
