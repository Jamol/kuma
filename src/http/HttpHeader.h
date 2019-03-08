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

#ifndef __HttpHeader_H__
#define __HttpHeader_H__

#include "kmdefs.h"
#include "kmapi.h"
#include "httpdefs.h"

KUMA_NS_BEGIN

class HttpHeader
{
public:
    HttpHeader(bool is_outgoing, bool is_http2=false);
    virtual ~HttpHeader() {}
    virtual KMError addHeader(std::string name, std::string value);
    virtual KMError addHeader(std::string name, uint32_t value);
    virtual bool removeHeader(const std::string &name);
    virtual bool removeHeaderValue(const std::string &name, const std::string &value);
    bool hasHeader(const std::string &name) const;
    const std::string& getHeader(const std::string &name) const;
    std::string buildHeader(const std::string &method, const std::string &url, const std::string &ver);
    std::string buildHeader(int status_code, const std::string &desc, const std::string &ver, const std::string &req_method);
    bool hasBody() const { return has_body_; }
    bool hasContentLength() const { return has_content_length_; }
    bool isChunked() const { return is_chunked_; }
    size_t getContentLength() const { return content_length_; }
    virtual void reset();
    void setHeaders(const HeaderVector &headers);
    void setHeaders(HeaderVector &&headers);
    HeaderVector& getHeaders() { return header_vec_; }
    const HeaderVector& getHeaders() const { return header_vec_; }
    
    void processHeader();
    void processHeader(int status_code, const std::string &req_method);
    
    HttpHeader& operator= (const HttpHeader &other);
    HttpHeader& operator= (HttpHeader &&other);
    
protected:
    bool                    is_http2_ = false;
    bool                    is_outgoing_ = true;
    bool                    is_chunked_ = false;
    bool                    has_content_length_ = false;
    bool                    has_body_ = false;
    size_t                  content_length_ = 0;
    HeaderVector            header_vec_;
};

KUMA_NS_END

#endif /* __HttpHeader_H__ */
