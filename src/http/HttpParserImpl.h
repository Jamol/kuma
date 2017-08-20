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

#ifndef __HttpParserImpl_H__
#define __HttpParserImpl_H__

#include "kmdefs.h"
#include "kmapi.h"
#include "httpdefs.h"
#include "util/util.h"
#include "util/DestroyDetector.h"
#include "HttpHeader.h"
#include <string>
#include <map>
#include <vector>
#include <functional>

KUMA_NS_BEGIN

class HttpParser::Impl : public DestroyDetector, public HttpHeader
{
public:
    using DataCallback = HttpParser::DataCallback;
    using EventCallback = HttpParser::EventCallback;
    using EnumrateCallback = std::function<void(const std::string&, const std::string&)>;
    
    Impl() = default;
    Impl(const Impl& other);
    Impl(Impl&& other);
    ~Impl();
    Impl& operator=(const Impl& other);
    Impl& operator=(Impl&& other);
    
    // return bytes parsed
    int parse(char* data, size_t len);
    void pause();
    void resume();
    // true - http completed
    bool setEOF();
    void reset();
    
    bool isRequest() const { return is_request_; }
    bool headerComplete() const { return header_complete_; }
    bool complete() const;
    bool error() const;
    bool paused() const { return paused_; }
    bool isUpgradeTo(const std::string& proto) const;
    
    int getStatusCode() const { return status_code_; }
    const std::string& getLocation() const { return getHeaderValue("Location"); }
    const std::string& getUrl() const { return url_; }
    const std::string& getUrlPath() const { return url_path_; }
    const std::string& getMethod() const { return method_; }
    const std::string& getVersion() const { return version_; }
    const std::string& getParamValue(const std::string& name) const;
    const std::string& getHeaderValue(const std::string& name) const;
    
    void forEachParam(EnumrateCallback cb);
    void forEachHeader(EnumrateCallback cb);
    
    void setDataCallback(DataCallback cb) { data_cb_ = std::move(cb); }
    void setEventCallback(EventCallback cb) { event_cb_ = std::move(cb); }
    
public:
    void setMethod(std::string m);
    void setUrl(std::string url);
    void setUrlPath(std::string path);
    void setVersion(std::string ver);
    void setHeaders(const HeaderVector & headers);
    void setHeaders(HeaderVector && headers);
    void addParamValue(std::string name, std::string value);
    void addHeaderValue(std::string name, std::string value);
    
private:
    typedef enum{
        PARSE_STATE_CONTINUE,
        PARSE_STATE_DONE,
        PARSE_STATE_ERROR,
        PARSE_STATE_DESTROYED
    }ParseState;
    
    typedef enum{
        HTTP_READ_LINE,
        HTTP_READ_HEAD,
        HTTP_READ_BODY,
        HTTP_READ_DONE,
        HTTP_READ_ERROR,
    }HttpReadState;
    
    typedef enum{
        CHUNK_READ_SIZE,
        CHUNK_READ_DATA,
        CHUNK_READ_DATA_CR,
        CHUNK_READ_DATA_LF,
        CHUNK_READ_DATACRLF,
        CHUNK_READ_TRAILER
    }ChunkReadState;
    
private:
    ParseState parseHttp(char*& cur_pos, char* end);
    bool parseStartLine(const char* line, const char* line_end);
    bool parseHeaderLine(const char* line, const char* line_end);
    ParseState parseChunk(char*& cur_pos, char* end);
    bool getLine(char*& cur_pos, char* end, const char*& line, const char*& line_end);
    
    bool decodeUrl();
    bool parseUrl();
    
    bool hasBody();
    bool readEOF();
    
    void onHeaderComplete();
    void onComplete();
    
    KMError saveData(const char* cur_pos, const char* end);
    bool bufferEmpty() { return str_buf_.empty(); };
    void clearBuffer() { str_buf_.clear(); }
    
private:
    DataCallback        data_cb_;
    EventCallback       event_cb_;
    bool                is_request_{ true };
    
    std::string         str_buf_;
    
    int                 read_state_{ HTTP_READ_LINE };
    bool                header_complete_{ false };
    bool                upgrade_{ false };
    bool                paused_{ false };
    
    int                 chunk_state_{ CHUNK_READ_SIZE };
    size_t              chunk_size_{ 0 };
    size_t              chunk_bytes_read_{ 0 };
    
    size_t              total_bytes_read_{ 0 };
    
    // request
    std::string         method_;
    std::string         url_;
    std::string         version_;
    std::string         url_path_;
    HeaderMap           param_map_;
    
    // response
    int                 status_code_{ 0 };
};

KUMA_NS_END

#endif
