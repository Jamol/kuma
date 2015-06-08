
#include "HttpParser.h"
#include "util/kmtrace.h"
#include "util/util.h"
#include "Uri.h"

KUMA_NS_BEGIN

#define CR  '\r'
#define LF  '\n'
#define MAX_HTTP_HEADER_SIZE	2*1024*1024 // 2 MB

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

//////////////////////////////////////////////////////////////////////////
HttpParser::HttpParser()
: is_request_(true)
, read_state_(HTTP_READ_LINE)
, header_complete_(false)
, has_content_length_(false)
, content_length_(0)
, is_chunked_(false)
, chunk_state_(CHUNK_READ_SIZE)
, chunk_size_(0)
, chunk_bytes_read_(0)
, total_bytes_read_(0)
, status_code_(0)
, destroy_flag_ptr_(nullptr)
{
    
}

HttpParser::~HttpParser()
{
    param_map_.clear();
    header_map_.clear();
    if(destroy_flag_ptr_) {
        *destroy_flag_ptr_ = true;
    }
}

void HttpParser::reset()
{
    read_state_ = HTTP_READ_LINE;
    
    status_code_ = 0;
    header_complete_ = false;
    content_length_ = 0;
    has_content_length_ = false;
    
    is_chunked_ = false;
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
    header_map_.clear();
}

bool HttpParser::complete()
{
    return HTTP_READ_DONE == read_state_;
}

bool HttpParser::error()
{
    return HTTP_READ_ERROR == read_state_;
}

bool HttpParser::hasBody()
{
    if(content_length_ > 0 || is_chunked_) {
        return true;
    }
    if(has_content_length_ && 0 == content_length_) {
        return false;
    }
    if(is_request_) {
        return false;
    } else { // read untill EOF
        return !((100 <= status_code_ && status_code_ <= 100) ||
                 204 == status_code_ || 304 == status_code_);
    }
}

int HttpParser::parse(const char* data, uint32_t len)
{
    if(HTTP_READ_DONE == read_state_ || HTTP_READ_ERROR == read_state_) {
        KUMA_WARNTRACE("parse, invalid state="<<read_state_);
        return 0;
    }
    if(HTTP_READ_BODY == read_state_ && !is_chunked_ && !has_content_length_)
    {// return directly, read untill EOF
        total_bytes_read_ += len;
        if(cb_data_) cb_data_(data, len);
        return len;
    }
    const char* pos = data;
    const char* end = data + len;
    ParseState parse_state = parseHttp(pos, end);
    int parsed_len = (int)(pos - data);
    if(PARSE_STATE_DESTROY == parse_state) {
        return parsed_len;
    }
    if(PARSE_STATE_ERROR == parse_state && cb_event_) {
        cb_event_(HTTP_ERROR);
    }
    return parsed_len;
}

int HttpParser::saveData(const char* cur_pos, const char* end)
{
    if(cur_pos == end) {
        return KUMA_ERROR_NOERR;
    }
    auto old_len = str_buf_.size();
    if((end - cur_pos) + old_len > MAX_HTTP_HEADER_SIZE) {
        return -1;
    }
    str_buf_.append(cur_pos, end);
    return KUMA_ERROR_NOERR;
}

HttpParser::ParseState HttpParser::parseHttp(const char*& cur_pos, const char* end)
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
            if(saveData(cur_pos, end) != KUMA_ERROR_NOERR) {
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
            if(line == line_end)
            {// blank line, header completed
                onHeaderComplete();
                if(hasBody()) {
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
            if(saveData(cur_pos, end) != KUMA_ERROR_NOERR) {
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
            uint32_t cur_len = uint32_t(end - cur_pos);
            if(has_content_length_ && (content_length_ - total_bytes_read_) <= cur_len)
            {// data enough
                const char* notify_data = cur_pos;
                uint32_t notify_len = content_length_ - total_bytes_read_;
                cur_pos += notify_len;
                total_bytes_read_ = content_length_;
                read_state_ = HTTP_READ_DONE;
                KUMA_ASSERT(!destroy_flag_ptr_);
                bool destroyed = false;
                destroy_flag_ptr_ = &destroyed;
                if(cb_data_) cb_data_(notify_data, notify_len);
                if(destroyed) {
                    return PARSE_STATE_DESTROY;
                }
                destroy_flag_ptr_ = nullptr;
                onComplete();
                return PARSE_STATE_DONE;
            }
            else
            {// need more data, or read untill EOF
                const char* notify_data = cur_pos;
                total_bytes_read_ += cur_len;
                cur_pos = end;
                if(cb_data_) cb_data_(notify_data, cur_len);
                return PARSE_STATE_CONTINUE;
            }
        }
    }
    return HTTP_READ_DONE == read_state_?PARSE_STATE_DONE:PARSE_STATE_CONTINUE;
}

bool HttpParser::parseStartLine(const char* line, const char* line_end)
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
        status_code_ = atoi(str.c_str());
    }
    clearStrBuf();
    return true;
}

bool HttpParser::parseHeaderLine(const char* line, const char* line_end)
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
        clearStrBuf();
        return false;
    }
    str_name.assign(p_line, p);
    p_line = p + 1;
    str_value.assign(p_line, p_end);
    clearStrBuf();
    addHeaderValue(str_name, str_value);
    return true;
}

HttpParser::ParseState HttpParser::parseChunk(const char*& cur_pos, const char* end)
{
    const char* p_line = nullptr;
    const char* p_end = nullptr;
    bool b_line = false;
    while (cur_pos < end)
    {
        switch (chunk_state_) {
            case CHUNK_READ_SIZE:
            {
                b_line = getLine(cur_pos, end, p_line, p_end);
                if(!b_line)
                {// need more data, save remain data.
                    if(saveData(cur_pos, end) != KUMA_ERROR_NOERR) {
                        return PARSE_STATE_ERROR;
                    }
                    cur_pos = end;
                    return PARSE_STATE_CONTINUE;
                }
                std::string str;
                if(!str_buf_.empty()) {
                    str.swap(str_buf_);
                    clearStrBuf();
                }
                str.append(p_line, p_end);
                chunk_size_ = (uint32_t)strtol(str.c_str(), NULL, 16);
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
                uint32_t cur_len = uint32_t(end - cur_pos);
                if(chunk_size_ - chunk_bytes_read_ <= cur_len) {// data enough
                    const char* notify_data = cur_pos;
                    uint32_t notify_len = chunk_size_ - chunk_bytes_read_;
                    total_bytes_read_ += notify_len;
                    chunk_bytes_read_ = chunk_size_ = 0; // reset
                    chunk_state_ = CHUNK_READ_DATA_CR;
                    cur_pos += notify_len;
                    bool destroyed = false;
                    destroy_flag_ptr_ = &destroyed;
                    if(cb_data_) cb_data_(notify_data, notify_len);
                    if(destroyed) {
                        return PARSE_STATE_DESTROY;
                    }
                    destroy_flag_ptr_ = nullptr;
                } else {// need more data
                    const char* notify_data = cur_pos;
                    total_bytes_read_ += cur_len;
                    chunk_bytes_read_ += cur_len;
                    cur_pos += cur_len;
                    if(cb_data_) cb_data_(notify_data, cur_len);
                    return PARSE_STATE_CONTINUE;
                }
                break;
            }
            case CHUNK_READ_DATA_CR:
            {
                if(*cur_pos != CR) {
                    KUMA_ERRTRACE("parseChunk, can not find data CR");
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
                    KUMA_ERRTRACE("parseChunk, can not find data LF");
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
                if(b_line && p_line == p_end) {
                    // empty line, http completed
                    read_state_ = HTTP_READ_DONE;
                    onComplete();
                    return PARSE_STATE_DONE;
                } else { // need more data
                    // dont save data, ignore trailer
                    //if(save_data(data+pos, len-pos) != KUMA_ERROR_NOERR) {
                    //    return PARSE_STATE_ERROR;
                    //}
                    cur_pos = end; // all data was consumed
                }
                break;
            }
        }
    }
    return HTTP_READ_DONE == read_state_?PARSE_STATE_DONE:PARSE_STATE_CONTINUE;
}

void HttpParser::onHeaderComplete()
{
    header_complete_ = true;
    auto it = header_map_.find("Content-Length");
    if(it != header_map_.end()) {
        content_length_ = atoi(it->second.c_str());
        has_content_length_ = true;
        KUMA_INFOTRACE("onHeaderComplete, Content-Length="<<content_length_);
    }
    it = header_map_.find("Transfer-Encoding");
    if(it != header_map_.end()) {
        is_chunked_ = is_equal("chunked", it->second);
        KUMA_INFOTRACE("onHeaderComplete, Transfer-Encoding="<<it->second);
    }
    if(cb_event_) cb_event_(HTTP_HEADER_COMPLETE);
}

void HttpParser::onComplete()
{
    KUMA_INFOTRACE("HttpParser::onComplete");
    if(cb_event_) cb_event_(HTTP_COMPLETE);
}

bool HttpParser::decodeUrl()
{
    std::string new_url;
    int i = 0;
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
                new_url = p_str[i++];
                break;
        }
    }
    
    std::swap(url_, new_url);
    return true;
}

bool HttpParser::parseUrl()
{
    Uri uri;
    if(!uri.parse(url_)) {
        return false;
    }
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
            addParamValue(name, value);
            break;
        }
        std::string value(query.begin()+pos, query.begin()+pos1);
        pos = pos1 + 1;
        addParamValue(name, value);
    }
    
    return true;
}

void HttpParser::addParamValue(const std::string& name, const std::string& value)
{
    if(!name.empty()) {
        param_map_[name] = value;
    }
}

void HttpParser::addHeaderValue(std::string& name, std::string& value)
{
    trim_left(name);
    trim_right(name);
    trim_left(value);
    trim_right(value);
    if(!name.empty()) {
        header_map_[name] = value;
    }
}

const char* HttpParser::getParamValue(const char* name)
{
    auto it = param_map_.find(name);
    if (it != param_map_.end()) {
        return (*it).second.c_str();
    }
    return nullptr;
}

const char* HttpParser::getHeaderValue(const char* name)
{
    auto it = header_map_.find(name);
    if (it != header_map_.end()) {
        return (*it).second.c_str();
    }
    return nullptr;
}

void HttpParser::forEachParam(EnumrateCallback cb)
{
    for (auto &kv : param_map_) {
        cb(kv.first.c_str(), kv.second.c_str());
    }
}

void HttpParser::forEachHeader(EnumrateCallback cb)
{
    for (auto &kv : header_map_) {
        cb(kv.first.c_str(), kv.second.c_str());
    }
}

bool HttpParser::getLine(const char*& cur_pos, const char* end, const char*& line, const char*& line_end)
{
    const char* lf = std::find(cur_pos, end, LF);
    if(lf == cur_pos) {
        return false;
    }
    line = cur_pos;
    line_end = lf;
    cur_pos = line_end + 1;
    if(line != line_end && *(line_end - 1) == CR) {
        --line_end;
    }
    return true;
}

KUMA_NS_END
