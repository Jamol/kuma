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

#include "PushClient.h"
#include "h2utils.h"
#include "http/HttpCache.h"
#include "H2ConnectionImpl.h"

KUMA_NS_USING

PushClient::PushClient()
{
    
}

PushClient::~PushClient()
{
    reset();
}

void PushClient::reset()
{
    if (stream_) {
        stream_->close();
        stream_.reset();
    }
}

void PushClient::releaseSelf()
{
    reset();
    uint32_t push_id = push_id_;
    push_id_ = 0;
    if(conn_ && push_id != 0) {
        conn_->removePushClient(push_id);
    }
}

KMError PushClient::attachStream(H2ConnectionImpl* conn, H2StreamPtr &stream)
{
    stream_ = stream;
    if (!stream_) {
        return KMError::INVALID_STATE;
    }
    push_id_ = stream_->getStreamId();
    conn_ = conn;
    stream_->setPromiseCallback([this] (const HeaderVector &headers) {
        onPromise(headers);
    });
    stream_->setHeadersCallback([this] (const HeaderVector &headers, bool endSteam) {
        onHeaders(headers, endSteam);
    });
    stream_->setDataCallback([this] (KMBuffer &buf, bool endSteam) {
        onData(buf, endSteam);
    });
    stream_->setRSTStreamCallback([this] (int err) {
        onRSTStream(err);
    });
    stream_->setWriteCallback([this] {
        onWrite();
    });
    return KMError::NOERR;
}

H2StreamPtr PushClient::release()
{
    auto stream = std::move(stream_);
    releaseSelf();
    return stream;
}

void PushClient::onPromise(const HeaderVector &headers)
{
    for (auto &kv : headers) {
        auto &name = kv.first;
        auto &value = kv.second;
        if (!name.empty()) {
            if (name[0] == ':') { // pseudo header
                if (name == H2HeaderMethod) {
                    req_method_ = value;
                } else if (name == H2HeaderAuthority) {
                    req_host_ = value;
                } else if (name == H2HeaderPath) {
                    req_path_ = value;
                } else if (name == H2HeaderScheme) {
                    req_scheme_ = value;
                }
            }
        }
    }
    if (req_path_.empty()) {
        onError(KMError::INVALID_PARAM);
    }
}

void PushClient::onHeaders(const HeaderVector &headers, bool end_stream)
{
    if (!processH2ResponseHeaders(headers, status_code_, rsp_headers_)) {
        onError(KMError::INVALID_PARAM);
    }
    header_complete_ = true;
    if (end_stream) {
        onComplete();
    }
}

void PushClient::onData(KMBuffer &buf, bool end_stream)
{
    if (buf.chainLength() > 0) {
        if (rsp_body_) {
            rsp_body_->append(buf.clone());
        } else {
            rsp_body_.reset(buf.clone());
        }
    }
    if (end_stream) {
        onComplete();
    }
}

void PushClient::onRSTStream(int err)
{
    onError(KMError::FAILED);
}

void PushClient::onWrite()
{
    
}

void PushClient::onError(KMError err)
{
    releaseSelf();
}

void PushClient::onComplete()
{
    complete_ = true;
}

std::string PushClient::getCacheKey() const
{
    return req_host_ + req_path_;
}

void PushClient::getResponseHeaders(HeaderVector &rsp_headers)
{
    rsp_headers = std::move(rsp_headers_);
}

void PushClient::getResponseBody(KMBuffer &rsp_body)
{
    if (rsp_body_) {
        rsp_body = *rsp_body_;
    } else {
        rsp_body.reset();
    }
}
