/* Copyright © 2014-2017, Fengping Bao <jamol@live.com>
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

#ifndef __PushClient_H__
#define __PushClient_H__

#include "h2defs.h"
#include "H2Stream.h"

#include <memory>

KUMA_NS_BEGIN

class PushClient final
{
public:
    PushClient();
    ~PushClient();
    
    KMError attachStream(H2Connection::Impl* conn, H2StreamPtr &stream);
    bool isHeaderComplete() const { return header_complete_; }
    bool isComplete() const { return complete_; }
    
    std::string getCacheKey() const;
    void getResponseHeaders(HeaderVector &rsp_headers);
    void getResponseBody(KMBuffer &rsp_body);
    
    H2StreamPtr release();
    
protected:
    void onPromise(const HeaderVector &headers);
    void onHeaders(const HeaderVector &headers, bool end_stream);
    void onData(KMBuffer &buf, bool end_stream);
    void onRSTStream(int err);
    void onWrite();
    
    void onError(KMError err);
    void onComplete();
    
    void reset();
    void releaseSelf();
    
protected:
    H2StreamPtr stream_;
    H2Connection::Impl* conn_ = nullptr;
    uint32_t push_id_ = 0;
    
    std::string req_method_;
    std::string req_scheme_;
    std::string req_path_;
    std::string req_host_;
    
    bool header_complete_ = false;
    bool complete_ = false;
    int status_code_ = 0;
    HeaderVector rsp_headers_;
    KMBuffer::Ptr rsp_body_;
};

using PushClientPtr = std::unique_ptr<PushClient>;

KUMA_NS_END

#endif /* __PushClient_H__ */
