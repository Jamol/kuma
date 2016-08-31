/* Copyright (c) 2016, Fengping Bao <jamol@live.com>
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

#ifndef __SocketNotifier_H__
#define __SocketNotifier_H__

#include "util/util.h"
#include "Notifier.h"

KUMA_NS_BEGIN

class SocketNotifier : public Notifier
{
public:
    enum {
        READ_FD = 0,
        WRITE_FD
    };
    ~SocketNotifier() {
        cleanup();
    }
    bool init() override {
        cleanup();

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
    }
    bool ready() override {
        return fds_[READ_FD] != INVALID_FD && fds_[WRITE_FD] != INVALID_FD;
    }
    void notify() {
        uint64_t d = 1;
        sendto(fds_[WRITE_FD], (const char *)&d, sizeof(d), 0, (struct sockaddr*)&ss_addr_, addr_len_);
    }
    
    SOCKET_FD getReadFD() override {
        return fds_[READ_FD];
    }
    
    KMError onEvent(uint32_t ev) override {
        char buf[1024];
        recvfrom(fds_[READ_FD], buf, sizeof(buf), 0, NULL, NULL);
        return KMError::NOERR;
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

    SOCKET_FD fds_[2] { INVALID_FD, INVALID_FD };
    sockaddr_storage ss_addr_;
    socklen_t addr_len_;
};

KUMA_NS_END

#endif
