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

#include "internal.h"
#include "util/util.h"

KUMA_NS_BEGIN

class Notifier : public IOHandler
{
public:
    enum {
        READ_FD = 0,
        WRITE_FD
    };
    Notifier() {
        fds[READ_FD] = INVALID_FD;
        fds[WRITE_FD] = INVALID_FD;
    }
    ~Notifier() {
        if(fds[READ_FD] != INVALID_FD){
            ::closesocket(fds[READ_FD]);
            fds[READ_FD] = INVALID_FD;
        }
        if(fds[WRITE_FD] != INVALID_FD){
            ::closesocket(fds[WRITE_FD]);
            fds[WRITE_FD] = INVALID_FD;
        }
    }
    bool init() {
        fds[READ_FD] = socket(AF_INET, SOCK_DGRAM, 0);
        fds[WRITE_FD] = socket(AF_INET, SOCK_DGRAM, 0);
        struct addrinfo hints = {0};
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_DGRAM;
        hints.ai_flags = AI_ADDRCONFIG; // will block 10 seconds in some case if not set AI_ADDRCONFIG
        if(km_set_sock_addr("127.0.0.1", 0, &hints, (struct sockaddr*)&ss_addr, sizeof(ss_addr)) != 0) {
            return false;
        }
        if(bind(fds[READ_FD], (const sockaddr*)&ss_addr, sizeof(sockaddr_in)) != 0) {
            ::closesocket(fds[READ_FD]);
            ::closesocket(fds[WRITE_FD]);
            return false;
        }
        if(bind(fds[WRITE_FD], (const sockaddr*)&ss_addr, sizeof(sockaddr_in)) != 0) {
            ::closesocket(fds[READ_FD]);
            ::closesocket(fds[WRITE_FD]);
            return false;
        }
        set_nonblocking(fds[READ_FD]);
        set_nonblocking(fds[WRITE_FD]);
        addr_len = sizeof(ss_addr);
        if(getsockname(fds[READ_FD], (struct sockaddr*)&ss_addr, &addr_len) != 0) {
            ::closesocket(fds[READ_FD]);
            ::closesocket(fds[WRITE_FD]);
            return false;
        }
        return true;
    }
    void notify() {
        char ch = 1;
        sendto(fds[WRITE_FD], &ch, 1, 0, (struct sockaddr*)&ss_addr, addr_len);
    }
    
    int getReadFD() {
        return fds[READ_FD];
    }
    
    virtual long acquireReference() { return 1; }
    virtual long releaseReference() { return 1; }
    virtual int onEvent(uint32_t ev) {
        char buf[1024];
        recvfrom(fds[READ_FD], buf, sizeof(buf), 0, NULL, NULL);
        return KUMA_ERROR_NOERR;
    }
private:
    int fds[2];
    sockaddr_storage    ss_addr;
    socklen_t   addr_len;
};

KUMA_NS_END

#endif
