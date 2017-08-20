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

#include "HttpParserImpl.h"
#include "util/kmtrace.h"
#include "util/util.h"
#include "Uri.h"

#include <algorithm>

using namespace kuma;

namespace {

#define CR  '\r'
#define LF  '\n'
#define MAX_HTTP_HEADER_SIZE	2*1024*1024 // 2 MB

}

//////////////////////////////////////////////////////////////////////////
// HttpParserImpl
HttpParser::Impl::Impl(const Impl& other)
{
    *this = other;
}

HttpParser::Impl::Impl(Impl&& other)
{
    *this = std::move(other);
}

HttpParser::Impl& HttpParser::Impl::operator=(const Impl& other)
{
    if(this != &other) {
        is_request_ = other.is_request_;
        str_buf_ = other.str_buf_;
        read_state_ = other.read_state_;
        header_complete_ = other.header_complete_;
        upgrade_ = other.upgrade_;
        paused_ = other.paused_;
        has_content_length_ = other.has_content_length_;
        content_length_ = other.content_length_;
        
        is_chunked_ = other.is_chunked_;
        chunk_state_ = other.chunk_state_;
        chunk_size_ = other.chunk_size_;
        chunk_bytes_read_ = other.chunk_bytes_read_;
        total_bytes_read_ = other.total_bytes_read_;
        
        method_ = other.method_;
        url_ = other.url_;
        url_path_ = other.url_path_;
        param_map_ = other.param_map_;
        header_vec_ = other.header_vec_;
        status_code_ = other.status_code_;
    }
    return *this;
}

HttpParser::Impl& HttpParser::Impl::operator=(Impl&& other)
{
    if(this != &other) {
        is_request_ = other.is_request_;
        str_buf_.swap(other.str_buf_);
        read_state_ = other.read_state_;
        header_complete_ = other.header_complete_;
        upgrade_ = other.upgrade_;
        paused_ = other.paused_;
        has_content_length_ = other.has_content_length_;
        content_length_ = other.content_length_;
        has_body_ = other.has_body_;
        
        is_chunked_ = other.is_chunked_;
        chunk_state_ = other.chunk_state_;
        chunk_size_ = other.chunk_size_;
        chunk_bytes_read_ = other.chunk_bytes_read_;
        total_bytes_read_ = other.total_bytes_read_;
        
        method_.swap(other.method_);
        url_.swap(other.url_);
        url_path_.swap(other.url_path_);
        param_map_.swap(other.param_map_);
        header_vec_.swap(other.header_vec_);
        status_code_ = other.status_code_;
    }
    return *this;
}

HttpParser::Impl::~Impl()
{
    param_map_.clear();
    header_vec_.clear();
}

void HttpParser::Impl::reset()
{
    HttpHeader::reset();
    read_state_ = HTTP_READ_LINE;
    
    status_code_ = 0;
    header_complete_ = false;
    upgrade_ = false;
    paused_ = false;

    chunk_state_ = CHUNK_READ_SIZE;
    chunk_size_ = 0;
    chunk_bytes_read_ = 0;
    
    total_bytes_read_ = 0;
    str_buf_.clear();
    
    method_ = "";
    url_ = "";
    version_ = "";
    url_path_ = "";
    
    param_map_.clear();
}

bool HttpParser::Impl::complete() const
{
    return HTTP_READ_DONE == read_state_;
}

bool HttpParser::Impl::error() const
{
    return HTTP_READ_ERROR == read_state_;
}

bool HttpParser::Impl::isUpgradeTo(const std::string& proto) const
{
    if (!is_equal(getHeaderValue("Upgrade"), proto) ||
        !contains_token(getHeaderValue("Connection"), "Upgrade", ',')) {
        return false;
    }
    if (!isRequest() && 101 != getStatusCode()) {
        return false;
    }
    if (isRequest() && is_equal(proto, "h2c") &&
        !contains_token(getHeaderValue("Connection"), "HTTP2-Settings", ',')) {
        return false;
    }
    return true;
}

void HttpParser::Impl::pause() {
    paused_ = true;
}

void HttpParser::Impl::resume() {
    paused_ = false;
    if(hasBody() && !upgrade_) {
        read_state_ = HTTP_READ_BODY;
    } else {
        read_state_ = HTTP_READ_DONE;
        onComplete();
    }
}

bool HttpParser::Impl::readEOF()
{
    return !is_request_ && !has_content_length_ && !is_chunked_ &&
           !((100 <= status_code_ && status_code_ <= 199) ||
             204 == status_code_ || 304 == status_code_);
}

bool HttpParser::Impl::hasBody()
{
    return HttpHeader::hasBody();
}

int HttpParser::Impl::parse(char* data, size_t len)
{
    if(HTTP_READ_DONE == read_state_ || HTTP_READ_ERROR == read_state_) {
        KUMA_WARNTRACE("HttpParser::parse, invalid state="<<read_state_);
        return 0;
    }
    if(HTTP_READ_BODY == read_state_ && !is_chunked_ && !has_content_length_)
    {// return directly, read untill EOF
        total_bytes_read_ += len;
        if(data_cb_) data_cb_(data, len);
        return (int)len;
    }
    char* pos = data;
    char* end = data + len;
    ParseState parse_state = parseHttp(pos, end);
    int parsed_len = (int)(pos - data);
    if(PARSE_STATE_DESTROYED == parse_state) {
        return parsed_len;
    }
    if(PARSE_STATE_ERROR == parse_state && event_cb_) {
        event_cb_(HttpEvent::HTTP_ERROR);
    }
    return parsed_len;
}

bool HttpParser::Impl::setEOF()
{
    if(readEOF() && HTTP_READ_BODY == read_state_) {
        read_state_ = HTTP_READ_DONE;
        onComplete();
        return true;
    }
    return false;
}

KMError HttpParser::Impl::saveData(const char* cur_pos, const char* end)
{
    if(cur_pos == end) {
        return KMError::NOERR;
    }
    auto old_len = str_buf_.size();
    if((end - cur_pos) + old_len > MAX_HTTP_HEADER_SIZE) {
        return KMError::BUFFER_TOO_SMALL;
    }
    str_buf_.append(cur_pos, end);
    return KMError::NOERR;
}

HttpParser::Impl::ParseState HttpParser::Impl::parseHttp(char*& cur_pos, char* end)
{
    const char* line = nullptr;
    const char* line_end = nullptr;
    bool b_line = false;
    
    if(HTTP_READ_LINE == read_state_)
    {// try to get status line
        while ((b_line = getLine(cur_pos, end, line, line_end)) && line == line_end && str_buf_.empty())
            ;
        if(b_line && (line != line_end || !str_buf_.empty())) {
            if(!parseStartLine(line, line_end)) {
                read_state_ = HTTP_READ_ERROR;
                return PARSE_STATE_ERROR;
            }
            read_state_ = HTTP_READ_HEAD;
        } else {
            // need more data
            if(saveData(cur_pos, end) != KMError::NOERR) {
                return PARSE_STATE_ERROR;
            }
            cur_pos = end; // all data was consumed
            return PARSE_STATE_CONTINUE;
        }
    }
    if(HTTP_READ_HEAD == read_state_)
    {
        while ((b_line = getLine(cur_pos, end, line, line_end)))
        {
            if(line == line_end && bufferEmpty())
            {// blank line, header completed
                DESTROY_DETECTOR_SETUP();
                onHeaderComplete();
                DESTROY_DETECTOR_CHECK(PARSE_STATE_DESTROYED);
                if(paused_) {
                    return PARSE_STATE_CONTINUE;
                }
                if(hasBody() && !upgrade_) {
                    read_state_ = HTTP_READ_BODY;
                } else {
                    read_state_ = HTTP_READ_DONE;
                    onComplete();
                    return PARSE_STATE_DONE;
                }
                break;
            }
            parseHeaderLine(line, line_end);
        }
        if(HTTP_READ_HEAD == read_state_)
        {// need more data
            if(saveData(cur_pos, end) != KMError::NOERR) {
                return PARSE_STATE_ERROR;
            }
            cur_pos = end; // all data was consumed
            return PARSE_STATE_CONTINUE;
        }
    }
    if(HTTP_READ_BODY == read_state_ && cur_pos < end)
    {// try to get body
        if(is_chunked_) {
            return parseChunk(cur_pos, end);
        } else {
            size_t cur_len = end - cur_pos;
            if(has_content_length_ && (content_length_ - total_bytes_read_) <= cur_len)
            {// data enough
                char* notify_data = cur_pos;
                size_t notify_len = content_length_ - total_bytes_read_;
                cur_pos += notify_len;
                total_bytes_read_ = content_length_;
                read_state_ = HTTP_READ_DONE;
                DESTROY_DETECTOR_SETUP();
                if(data_cb_) data_cb_(notify_data, notify_len);
                DESTROY_DETECTOR_CHECK(PARSE_STATE_DESTROYED);
                onComplete();
                return PARSE_STATE_DONE;
            }
            else
            {// need more data, or read untill EOF
                char* notify_data = cur_pos;
                total_bytes_read_ += cur_len;
                cur_pos = end;
                if(data_cb_) data_cb_(notify_data, cur_len);
                return PARSE_STATE_CONTINUE;
            }
        }
    }
    return HTTP_READ_DONE == read_state_?PARSE_STATE_DONE:PARSE_STATE_CONTINUE;
}

bool HttpParser::Impl::parseStartLine(const char* line, const char* line_end)
{
    const char* p_line = line;
    const char* p_end = line_end;
    if(!str_buf_.empty()) {
        str_buf_.append(line, line_end);
        p_line = str_buf_.c_str();
        p_end = p_line + str_buf_.length();
    }
    std::string str;
    const char* p = std::find(p_line, p_end, ' ');
    if(p != p_end) {
        str.assign(p_line, p);
        p_line = p + 1;
    } else {
        return false;
    }
    is_request_ = !is_equal(str, "HTTP", 4);
    if(is_request_) {// request
        method_.swap(str);
        p = std::find(p_line, p_end, ' ');
        if(p != p_end) {
            url_.assign(p_line, p);
            p_line = p + 1;
        } else {
            return false;
        }
        version_.assign(p_line, p_end);
        decodeUrl();
        parseUrl();
    } else {// response
        version_.swap(str);
        p = std::find(p_line, p_end, ' ');
        str.assign(p_line, p);
        status_code_ = std::stoi(str);
    }
    clearBuffer();
    return true;
}

bool HttpParser::Impl::parseHeaderLine(const char* line, const char* line_end)
{
    const char* p_line = line;
    const char* p_end = line_end;
    if(!str_buf_.empty()) {
        str_buf_.append(line, line_end);
        p_line = str_buf_.c_str();
        p_end = p_line + str_buf_.length();
    }
    
    std::string str_name;
    std::string str_value;
    const char* p = std::find(p_line, p_end, ':');
    if(p == p_end) {
        clearBuffer();
        return false;
    }
    str_name.assign(p_line, p);
    p_line = p + 1;
    str_value.assign(p_line, p_end);
    clearBuffer();
    addHeaderValue(std::move(str_name), std::move(str_value));
    return true;
}

HttpParser::Impl::ParseState HttpParser::Impl::parseChunk(char*& cur_pos, char* end)
{
    const char* p_line = nullptr;
    const char* p_end = nullptr;
    bool b_line = false;
    while (cur_pos < end)
    {
        switch (chunk_state_)
        {
            case CHUNK_READ_SIZE:
            {
                b_line = getLine(cur_pos, end, p_line, p_end);
                if(!b_line)
                {// need more data, save remain data.
                    if(saveData(cur_pos, end) != KMError::NOERR) {
                        return PARSE_STATE_ERROR;
                    }
                    cur_pos = end;
                    return PARSE_STATE_CONTINUE;
                }
                std::string str;
                if(!str_buf_.empty()) {
                    str.swap(str_buf_);
                    clearBuffer();
                }
                str.append(p_line, p_end);
                // need not parse chunk extension
                chunk_size_ = strtol(str.c_str(), NULL, 16);
                KUMA_INFOTRACE("HttpParser::parseChunk, chunk_size="<<chunk_size_);
                if(0 == chunk_size_)
                {// chunk completed
                    chunk_state_ = CHUNK_READ_TRAILER;
                } else {
                    chunk_bytes_read_ = 0;
                    chunk_state_ = CHUNK_READ_DATA;
                }
                break;
            }
            case CHUNK_READ_DATA:
            {
                size_t cur_len = end - cur_pos;
                if(chunk_size_ - chunk_bytes_read_ <= cur_len) {// data enough
                    char* notify_data = cur_pos;
                    size_t notify_len = chunk_size_ - chunk_bytes_read_;
                    total_bytes_read_ += notify_len;
                    chunk_bytes_read_ = chunk_size_ = 0; // reset
                    chunk_state_ = CHUNK_READ_DATA_CR;
                    cur_pos += notify_len;
                    DESTROY_DETECTOR_SETUP();
                    if(data_cb_) data_cb_(notify_data, notify_len);
                    DESTROY_DETECTOR_CHECK(PARSE_STATE_DESTROYED);
                } else {// need more data
                    char* notify_data = cur_pos;
                    total_bytes_read_ += cur_len;
                    chunk_bytes_read_ += cur_len;
                    cur_pos += cur_len;
                    if(data_cb_) data_cb_(notify_data, cur_len);
                    return PARSE_STATE_CONTINUE;
                }
                break;
            }
            case CHUNK_READ_DATA_CR:
            {
                if(*cur_pos != CR) {
                    KUMA_ERRTRACE("HttpParser::parseChunk, can not find data CR");
                    read_state_ = HTTP_READ_ERROR;
                    return PARSE_STATE_ERROR;
                }
                ++cur_pos;
                chunk_state_ = CHUNK_READ_DATA_LF;
                break;
            }
            case CHUNK_READ_DATA_LF:
            {
                if(*cur_pos != LF) {
                    KUMA_ERRTRACE("HttpParser::parseChunk, can not find data LF");
                    read_state_ = HTTP_READ_ERROR;
                    return PARSE_STATE_ERROR;
                }
                ++cur_pos;
                chunk_state_ = CHUNK_READ_SIZE;
                break;
            }
            case CHUNK_READ_TRAILER:
            {
                b_line = getLine(cur_pos, end, p_line, p_end);
                if(b_line) {
                    if(p_line == p_end && bufferEmpty()) {
                        // blank line, http completed
                        read_state_ = HTTP_READ_DONE;
                        onComplete();
                        return PARSE_STATE_DONE;
                    }
                    clearBuffer(); // discard trailer
                } else { // need more data
                    if(saveData(cur_pos, end) != KMError::NOERR) {
                        return PARSE_STATE_ERROR;
                    }
                    cur_pos = end; // all data was consumed
                    return PARSE_STATE_CONTINUE;
                }
                break;
            }
        }
    }
    return HTTP_READ_DONE == read_state_?PARSE_STATE_DONE:PARSE_STATE_CONTINUE;
}

void HttpParser::Impl::onHeaderComplete()
{
    if (isRequest()) {
        HttpHeader::processHeader();
    } else {
        HttpHeader::processHeader(status_code_);
    }
    header_complete_ = true;
    if(has_content_length_) {
        KUMA_INFOTRACE("HttpParser::onHeaderComplete, Content-Length="<<content_length_);
    }
    if(is_chunked_) {
        KUMA_INFOTRACE("HttpParser::onHeaderComplete, Transfer-Encoding=chunked");
    }
    auto upgrade_to = HttpHeader::getHeader(strUpgrade);
    if(!upgrade_to.empty()) {
        upgrade_ = true;
        KUMA_INFOTRACE("HttpParser::onHeaderComplete, Upgrade="<<upgrade_to);
    }
    if(event_cb_) event_cb_(HttpEvent::HEADER_COMPLETE);
}

void HttpParser::Impl::onComplete()
{
    KUMA_INFOTRACE("HttpParser::onComplete");
    if(event_cb_) event_cb_(HttpEvent::COMPLETE);
}

bool HttpParser::Impl::decodeUrl()
{
    std::string new_url;
    size_t i = 0;
    auto len = url_.length();
    const char * p_str = url_.c_str();
    while (i < len)
    {
        switch (p_str[i])
        {
            case '+':
                new_url.append(1, ' ');
                i++;
                break;
                
            case '%':
                if (p_str[i + 1] == '%') {
                    new_url.append(1, '%');
                    i += 2;
                } else if(p_str[i + 1] == '\0') {
                    return false;
                } else {
                    char ch1 = p_str[i + 1];
                    char ch2 = p_str[i + 2];
                    ch1 = ch1 >= 'A' ? ((ch1 & 0xdf) - 'A' + 10) : ch1 - '0';
                    ch2 = ch2 >= 'A' ? ((ch2 & 0xdf) - 'A' + 10) : ch2 - '0';
                    new_url.append(1, (char)(ch1*16 + ch2));
                    i += 3;
                }
                break;
                
            default:
                new_url.append(1, p_str[i++]);
                break;
        }
    }
    
    std::swap(url_, new_url);
    return true;
}

bool HttpParser::Impl::parseUrl()
{
    Uri uri;
    if(!uri.parse(url_)) {
        return false;
    }
    url_path_ = uri.getPath();
    const std::string& query = uri.getQuery();
    std::string::size_type pos = 0;
    while (true) {
        auto pos1 = query.find('=', pos);
        if(pos1 == std::string::npos){
            break;
        }
        std::string name(query.begin()+pos, query.begin()+pos1);
        pos = pos1 + 1;
        pos1 = query.find('&', pos);
        if(pos1 == std::string::npos){
            std::string value(query.begin()+pos, query.end());
            addParamValue(std::move(name), std::move(value));
            break;
        }
        std::string value(query.begin()+pos, query.begin()+pos1);
        pos = pos1 + 1;
        addParamValue(std::move(name), std::move(value));
    }
    
    return true;
}

void HttpParser::Impl::addParamValue(std::string name, std::string value)
{
    if(!name.empty()) {
        param_map_[std::move(name)] = std::move(value);
    }
}

void HttpParser::Impl::addHeaderValue(std::string name, std::string value)
{
    trim_left(name);
    trim_right(name);
    trim_left(value);
    trim_right(value);
    if(!name.empty()) {
        HttpHeader::addHeader(std::move(name), std::move(value));
    }
}

const std::string& HttpParser::Impl::getParamValue(const std::string& name) const
{
    auto it = param_map_.find(name);
    if (it != param_map_.end()) {
        return (*it).second;
    }
    return EmptyString;
}

const std::string& HttpParser::Impl::getHeaderValue(const std::string& name) const
{
    return HttpHeader::getHeader(name);
}

void HttpParser::Impl::forEachParam(EnumrateCallback cb)
{
    for (auto &kv : param_map_) {
        cb(kv.first, kv.second);
    }
}

void HttpParser::Impl::forEachHeader(EnumrateCallback cb)
{
    for (auto &kv : header_vec_) {
        cb(kv.first, kv.second);
    }
}

void HttpParser::Impl::setMethod(std::string m)
{
    method_ = std::move(m);
}

void HttpParser::Impl::setUrl(std::string url)
{
    url_ = std::move(url);
}

void HttpParser::Impl::setUrlPath(std::string path)
{
    url_path_ = std::move(path);
}

void HttpParser::Impl::setVersion(std::string ver)
{
    version_ = std::move(ver);
}

void HttpParser::Impl::setHeaders(const HeaderVector & headers)
{
    header_vec_ = headers;
}

void HttpParser::Impl::setHeaders(HeaderVector && headers)
{
    header_vec_ = std::move(headers);
}

bool HttpParser::Impl::getLine(char*& cur_pos, char* end, const char*& line, const char*& line_end)
{
    char* lf = std::find(cur_pos, end, LF);
    if(lf == end) {
        return false;
    }
    line = cur_pos;
    line_end = lf;
    cur_pos = lf + 1;
    if(line != line_end && *(line_end - 1) == CR) {
        --line_end;
    }
    return true;
}
