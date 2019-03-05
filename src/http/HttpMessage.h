/* Copyright © 2017, Fengping Bao <jamol@live.com>
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

#ifndef __HttpMessage_H__
#define __HttpMessage_H__

#include "kmdefs.h"
#include "kmapi.h"
#include "httpdefs.h"
#include "HttpHeader.h"


KUMA_NS_BEGIN

class HttpMessage : public HttpHeader
{
public:
    using MessageSender = std::function<int(const void*, size_t)>;
    using MessageVSender = std::function<int(const iovec*, int)>;
    using MessageBSender = std::function<int(const KMBuffer&)>;
    
    HttpMessage() : HttpHeader(true) {}
    int sendData(const void* data, size_t len);
    int sendData(const KMBuffer &buf);
    bool isCompleted() const { return !hasBody() || completed_; }
    void reset() override;
    
    void setSender(MessageSender sender) { sender_ = std::move(sender); }
    void setVSender(MessageVSender sender) { vsender_ = std::move(sender); }
    void setBSender(MessageBSender sender) { bsender_ = std::move(sender); }
    
protected:
    int sendChunk(const void* data, size_t len);
    int sendChunk(const KMBuffer &buf);
    
protected:
    bool                    completed_ = false;
    size_t                  body_bytes_sent_ = 0;
    
    MessageSender           sender_;
    MessageVSender          vsender_;
    MessageBSender          bsender_;
};

KUMA_NS_END

#endif /* __HttpMessage_H__ */
