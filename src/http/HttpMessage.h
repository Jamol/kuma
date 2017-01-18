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

KUMA_NS_BEGIN

class HttpMessage
{
public:
    using MessageSender = std::function<int(const void*, size_t)>;
    
    void addHeader(std::string name, std::string value);
    void addHeader(std::string name, uint32_t value);
    bool hasHeader(const std::string &name) const;
    std::string buildMessageHeader(const std::string &method, const std::string &url, const std::string &ver);
    std::string buildMessageHeader(int status_code, const std::string &desc, const std::string &ver);
    int sendData(const void* data, size_t len);
    bool hasBody() const { return has_body_; }
    bool isCompleted() const { return !has_body_ || completed_; }
    void reset();
    
protected:
    int sendChunk(const void* data, size_t len);
    void processHeaders();
    
protected:
    HeaderMap               header_map_;
    bool                    is_chunked_ = false;
    bool                    has_content_length_ = false;
    bool                    completed_ = false;
    bool                    has_body_ = false;
    size_t                  content_length_ = 0;
    size_t                  body_bytes_sent_ = 0;
    
    MessageSender           sender_;
};

KUMA_NS_END

#endif /* __HttpMessage_H__ */
