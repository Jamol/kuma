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

#ifndef __EventNotifier_H__
#define __EventNotifier_H__

#include "util/util.h"
#include "Notifier.h"

KUMA_NS_BEGIN

#include <errno.h>
#include <sys/eventfd.h>

class EventNotifier : public Notifier
{
public:
    ~EventNotifier() {
        cleanup();
    }
    bool init() override {
        cleanup();
        efd_ = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
        return efd_ >= 0;
    }
    bool ready() override {
        return efd_ >= 0;
    }
    void notify() override {
        if (efd_ >= 0) {
            //eventfd_write(efd_, 1);
            ssize_t ret = 0;
            do {
                uint64_t count = 1;
                ret = write(efd_, &count, sizeof(count));
            } while (ret < 0 && errno == EINTR);
        }
    }
    
    SOCKET_FD getReadFD() override {
        return efd_;
    }
    
    KMError onEvent(KMEvent ev) override {
        uint64_t count = 0;
        ssize_t ret = 0;
        do {
            //eventfd_t val;
            //eventfd_read(efd_, &val);
            ret = read(efd_, &count, sizeof(count));
        } while (ret < 0 && errno == EINTR);
        return KMError::NOERR;
    }
private:
    void cleanup() {
        if (efd_ != -1) {
            close(efd_);
            efd_ = -1;
        }
    }

    int efd_ { -1 };
};

KUMA_NS_END

#endif
