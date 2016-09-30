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

#ifndef __FlowControl_H__
#define __FlowControl_H__

#include "kmdefs.h"
#include "h2defs.h"

#include <functional>

KUMA_NS_BEGIN

class FlowControl
{
public:
    using UpdateCallback = std::function<void(uint32_t)>;
    
    FlowControl(uint32_t streamId, UpdateCallback cb);
    void setLocalWindowStep(uint32_t windowSize);
    void setMinLocalWindowSize(uint32_t minWindowSize);
    void updateRemoteWindowSize(ssize_t delta);
    void initLocalWindowSize(uint32_t windowSize);
    void initRemoteWindowSize(uint32_t windowSize);
    
    uint32_t localWindowSize();
    uint32_t remoteWindowSize();
    
    void bytesSent(size_t bytes);
    void bytesReceived(size_t bytes);
    
private:
    uint32_t streamId_ = 0;
    size_t localWindowStep_ = H2_DEFAULT_WINDOW_SIZE;
    ssize_t localWindowSize_ = H2_DEFAULT_WINDOW_SIZE;
    size_t minLocalWindowSize_ = 32768;
    size_t bytesReceived_ = 0;
    
    ssize_t remoteWindowSize_ = H2_DEFAULT_WINDOW_SIZE;
    size_t bytesSent_ = 0;
    
    UpdateCallback update_cb_;
};

KUMA_NS_END

#endif
