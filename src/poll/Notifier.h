/* Copyright (c) 2014, Fengping Bao <jamol@live.com>
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef kuma_Notifier_h
#define kuma_Notifier_h

#include "util/util.h"

KUMA_NS_BEGIN

#define USE_PIPE

class Notifier
{
public:
    enum {
        READ_FD = 0,
        WRITE_FD
    };
    Notifier() {
        fds_[READ_FD] = INVALID_FD;
        fds_[WRITE_FD] = INVALID_FD;
    }
    ~Notifier() {
        cleanup();
    }
    bool init() {
        cleanup();
#if defined(USE_PIPE) && !defined(KUMA_OS_WIN)
        if(pipe(fds_) != 0) {
            cleanup();
            return false;
        } else {
            int fl = fcntl(fds_[READ_FD], F_GETFL);
            fl |= O_NONBLOCK;
            fcntl(fds_[READ_FD], F_SETFL, fl);
            fl = fcntl(fds_[WRITE_FD], F_GETFL);
            fl |= O_NONBLOCK;
            fcntl(fds_[WRITE_FD], F_SETFL, fl);
            return true;
        }
#else
        fds_[READ_FD] = socket(AF_INET, SOCK_DGRAM, 0);
        fds_[WRITE_FD] = socket(AF_INET, SOCK_DGRAM, 0);
        struct addrinfo hints = {0};
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_DGRAM;
        hints.ai_flags = AI_ADDRCONFIG; // will block 10 seconds in some case if not set AI_ADDRCONFIG
        if(km_set_sock_addr("127.0.0.1", 0, &hints, (struct sockaddr*)&ss_addr_, sizeof(ss_addr_)) != 0) {
            cleanup();
            return false;
        }
        if(bind(fds_[READ_FD], (const sockaddr*)&ss_addr_, sizeof(sockaddr_in)) != 0) {
            cleanup();
            return false;
        }
        if(bind(fds_[WRITE_FD], (const sockaddr*)&ss_addr_, sizeof(sockaddr_in)) != 0) {
            cleanup();
            return false;
        }
        set_nonblocking(fds_[READ_FD]);
        set_nonblocking(fds_[WRITE_FD]);
        addr_len_ = sizeof(ss_addr_);
        if(getsockname(fds_[READ_FD], (struct sockaddr*)&ss_addr_, &addr_len_) != 0) {
            cleanup();
            return false;
        }
        return true;
#endif
    }
    bool ready() {
        return fds_[READ_FD] != INVALID_FD && fds_[WRITE_FD] != INVALID_FD;
    }
    void notify() {
#if defined(USE_PIPE) && !defined(KUMA_OS_WIN)
        if(fds_[WRITE_FD] != INVALID_FD) {
            int i = 1;
            write(fds_[WRITE_FD], &i, sizeof(i));
        }
#else
        uint64_t d = 1;
        sendto(fds_[WRITE_FD], (const char *)&d, sizeof(d), 0, (struct sockaddr*)&ss_addr_, addr_len_);
#endif
    }
    
    SOCKET_FD getReadFD() {
        return fds_[READ_FD];
    }
    
    int onEvent(uint32_t ev) {
        char buf[1024];
#if defined(USE_PIPE) && !defined(KUMA_OS_WIN)
        while(read(fds_[READ_FD], buf, sizeof(buf))>0) ;
#else
        recvfrom(fds_[READ_FD], buf, sizeof(buf), 0, NULL, NULL);
#endif
        return KUMA_ERROR_NOERR;
    }
private:
    void cleanup() {
        if (fds_[READ_FD] != INVALID_FD) {
            closeFd(fds_[READ_FD]);
            fds_[READ_FD] = INVALID_FD;
        }
        if (fds_[WRITE_FD] != INVALID_FD) {
            closeFd(fds_[WRITE_FD]);
            fds_[WRITE_FD] = INVALID_FD;
        }
    }

    SOCKET_FD fds_[2];
    sockaddr_storage ss_addr_;
    socklen_t addr_len_;
};

KUMA_NS_END

#endif
