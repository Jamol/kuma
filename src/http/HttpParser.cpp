
#include "HttpParser.h"
#include "util/kmtrace.h"
#include "util/util.h"
#include "Uri.h"

KUMA_NS_BEGIN

#define CR  '\r'
#define LF  '\n'
#define MAX_HTTP_HEADER_SIZE	10*1024*1024 // 10 MB

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

bool HttpParser::has_body()
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

int HttpParser::parse(uint8_t* data, uint32_t len)
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
    uint32_t read_len = 0;
    ParseState parse_state = parse_http(data, len, read_len);
    if(PARSE_STATE_DESTROY == parse_state) {
        return read_len;
    }
    if(PARSE_STATE_ERROR == parse_state && cb_event_) {
        cb_event_(HTTP_ERROR);
    }
    return read_len;
}

int HttpParser::save_data(uint8_t* data, uint32_t len)
{
    if(0 == len) {
        return KUMA_ERROR_NOERR;
    }
    auto old_len = str_buf_.size();
    if(len + old_len > MAX_HTTP_HEADER_SIZE) {
        return -1;
    }
    str_buf_.append((char*)data, len);
    return KUMA_ERROR_NOERR;
}

HttpParser::ParseState HttpParser::parse_http(uint8_t* data, uint32_t len, uint32_t& used_len)
{
    char* cur_line = NULL;
    uint32_t& pos = used_len;
    pos = 0;
    
    if(HTTP_READ_LINE == read_state_)
    {// try to get status line
        while ((cur_line = get_line((char*)data, len, pos)) && cur_line[0] == '\0' && str_buf_.empty())
            ;
        if(cur_line) {
            if(!parse_status_line(cur_line)) {
                read_state_ = HTTP_READ_ERROR;
                return PARSE_STATE_ERROR;
            }
            read_state_ = HTTP_READ_HEAD;
        } else {
            // need more data
            if(save_data(data+pos, len-pos) != KUMA_ERROR_NOERR) {
                return PARSE_STATE_ERROR;
            }
            pos = len; // all data was consumed
            return PARSE_STATE_CONTINUE;
        }
    }
    if(HTTP_READ_HEAD == read_state_)
    {
        while ((cur_line = get_line((char*)data, len, pos)) != nullptr)
        {
            if(cur_line[0] == '\0')
            {// blank line, header completed
                on_header_complete();
                if(has_body()) {
                    read_state_ = HTTP_READ_BODY;
                } else {
                    read_state_ = HTTP_READ_DONE;
                    on_complete();
                    return PARSE_STATE_DONE;
                }
                break;
            }
            parse_header_line(cur_line);
        }
        if(HTTP_READ_HEAD == read_state_)
        {// need more data
            if(save_data(data+pos, len-pos) != KUMA_ERROR_NOERR) {
                return PARSE_STATE_ERROR;
            }
            pos = len; // all data was consumed
            return PARSE_STATE_CONTINUE;
        }
    }
    if(HTTP_READ_BODY == read_state_ && (len - pos) > 0)
    {// try to get body
        if(is_chunked_) {
            return parse_chunk(data, len, pos);
        } else {
            uint32_t cur_len = len - pos;
            if(has_content_length_ && (content_length_ - total_bytes_read_) <= cur_len)
            {// data enough
                uint8_t* notify_data = data + pos;
                uint32_t notify_len = content_length_ - total_bytes_read_;
                pos += notify_len;
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
                on_complete();
                return PARSE_STATE_DONE;
            }
            else
            {// need more data, or read untill EOF
                uint8_t* notify_data = data + pos;
                total_bytes_read_ += cur_len;
                pos = len;
                if(cb_data_) cb_data_(notify_data, cur_len);
                return PARSE_STATE_CONTINUE;
            }
        }
    }
    return HTTP_READ_DONE == read_state_?PARSE_STATE_DONE:PARSE_STATE_CONTINUE;
}

bool HttpParser::parse_status_line(char* cur_line)
{
    const char* p_line = cur_line;
    if(!str_buf_.empty()) {
        str_buf_ += cur_line;
        p_line = str_buf_.c_str();
    }
    KUMA_INFOTRACE("parse_status_line, "<<p_line);
    std::string str;
    const char*  p = strchr(p_line, ' ');
    if(p) {
        str.assign(p_line, p);
        p_line = p + 1;
    } else {
        return false;
    }
    is_request_ = !is_equal(str, "HTTP", 4);
    if(is_request_) {// request
        method_.swap(str);
        p = strchr(p_line, ' ');
        if(p) {
            url_.assign(p_line, p);
            p_line = p + 1;
        } else {
            return false;
        }
        version_ = p_line;
        decode_url();
        parse_url();
    } else {// response
        version_.swap(str);
        p = strchr(p_line, ' ');
        if(p) {
            str.assign(p_line, p);
        } else {
            str = p_line;
        }
        status_code_ = atoi(str.c_str());
    }
    clear_buffer();
    return true;
}

bool HttpParser::parse_header_line(char * cur_line)
{
    const char* p_line = cur_line;
    if(!str_buf_.empty()) {
        str_buf_ += cur_line;
        p_line = str_buf_.c_str();
    }
    std::string str_name;
    std::string str_value;
    const char* p = strchr(p_line, ':');
    if(NULL == p) {
        clear_buffer();
        return false;
    }
    str_name.assign(p_line, p);
    p_line = p + 1;
    str_value = p_line;
    clear_buffer();
    add_header_value(str_name, str_value);
    return true;
}

HttpParser::ParseState HttpParser::parse_chunk(uint8_t* data, uint32_t len, uint32_t& pos)
{
    while (pos < len)
    {
        switch (chunk_state_) {
            case CHUNK_READ_SIZE:
            {
                char* cur_line = get_line((char*)data, len, pos);
                if(nullptr == cur_line)
                {// need more data, save remain data.
                    if(save_data(data+pos, len-pos) != KUMA_ERROR_NOERR) {
                        return PARSE_STATE_ERROR;
                    }
                    pos = len;
                    return PARSE_STATE_CONTINUE;
                }
                if(!str_buf_.empty()) {
                    str_buf_ += cur_line;
                    chunk_size_ = strtol(str_buf_.c_str(), NULL, 16);
                    clear_buffer();
                } else {
                    chunk_size_ = strtol(cur_line, NULL, 16);
                }
                KUMA_INFOTRACE("HttpParser::parse_chunk, chunk_size="<<chunk_size_);
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
                uint32_t cur_len = len - pos;
                if(chunk_size_ - chunk_bytes_read_ <= cur_len) {// data enough
                    uint8_t* notify_data = data + pos;
                    uint32_t notify_len = chunk_size_ - chunk_bytes_read_;
                    total_bytes_read_ += notify_len;
                    chunk_bytes_read_ = chunk_size_ = 0; // reset
                    chunk_state_ = CHUNK_READ_DATA_CR;
                    pos += notify_len;
                    bool destroyed = false;
                    destroy_flag_ptr_ = &destroyed;
                    if(cb_data_) cb_data_(notify_data, notify_len);
                    if(destroyed) {
                        return PARSE_STATE_DESTROY;
                    }
                    destroy_flag_ptr_ = nullptr;
                } else {// need more data
                    uint8_t* notify_data = data + pos;
                    total_bytes_read_ += cur_len;
                    chunk_bytes_read_ += cur_len;
                    pos += cur_len;
                    if(cb_data_) cb_data_(notify_data, cur_len);
                    return PARSE_STATE_CONTINUE;
                }
                break;
            }
            case CHUNK_READ_DATA_CR:
            {
                if(data[pos] != CR) {
                    KUMA_ERRTRACE("parse_chunk, can not find data CR");
                    read_state_ = HTTP_READ_ERROR;
                    return PARSE_STATE_ERROR;
                }
                ++pos;
                chunk_state_ = CHUNK_READ_DATA_LF;
                break;
            }
            case CHUNK_READ_DATA_LF:
            {
                if(data[pos] != LF) {
                    KUMA_ERRTRACE("parse_chunk, can not find data LF");
                    read_state_ = HTTP_READ_ERROR;
                    return PARSE_STATE_ERROR;
                }
                ++pos;
                chunk_state_ = CHUNK_READ_SIZE;
                break;
            }
            case CHUNK_READ_TRAILER:
            {
                char* cur_line = get_line((char*)data, len, pos);
                if(cur_line != nullptr && cur_line[0] == '\0') {
                    // empty line, http completed
                    read_state_ = HTTP_READ_DONE;
                    on_complete();
                    return PARSE_STATE_DONE;
                } else { // need more data
                    // dont save data, ignore trailer
                    //if(save_data(data+pos, len-pos) != KUMA_ERROR_NOERR) {
                    //    return PARSE_STATE_ERROR;
                    //}
                    pos = len; // all data was consumed
                }
                break;
            }
        }
    }
    return HTTP_READ_DONE == read_state_?PARSE_STATE_DONE:PARSE_STATE_CONTINUE;
}

void HttpParser::on_header_complete()
{
    header_complete_ = true;
    auto it = header_map_.find("Content-Length");
    if(it != header_map_.end()) {
        content_length_ = atoi(it->second.c_str());
        has_content_length_ = true;
        KUMA_INFOTRACE("on_header_complete, Content-Length="<<content_length_);
    }
    it = header_map_.find("Transfer-Encoding");
    if(it != header_map_.end()) {
        is_chunked_ = is_equal("chunked", it->second);
        KUMA_INFOTRACE("on_header_complete, Transfer-Encoding="<<it->second);
    }
    if(cb_event_) cb_event_(HTTP_HEADER_COMPLETE);
}

void HttpParser::on_complete()
{
    KUMA_INFOTRACE("HttpParser::on_complete");
    if(cb_event_) cb_event_(HTTP_COMPLETE);
}

bool HttpParser::decode_url()
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

bool HttpParser::parse_url()
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
            add_param_value(name, value);
            break;
        }
        std::string value(query.begin()+pos, query.begin()+pos1);
        pos = pos1 + 1;
        add_param_value(name, value);
    }
    
    return true;
}

void HttpParser::add_param_value(const std::string& name, const std::string& value)
{
    if(!name.empty()) {
        param_map_[name] = value;
    }
}

void HttpParser::add_header_value(std::string& name, std::string& value)
{
    trim_left(name);
    trim_right(name);
    trim_left(value);
    trim_right(value);
    if(!name.empty()) {
        header_map_[name] = value;
    }
}

const char* HttpParser::get_param_value(const char* name)
{
    auto it = param_map_.find(name);
    if (it != param_map_.end()) {
        return (*it).second.c_str();
    }
    return nullptr;
}

const char* HttpParser::get_header_value(const char* name)
{
    auto it = header_map_.find(name);
    if (it != header_map_.end()) {
        return (*it).second.c_str();
    }
    return nullptr;
}

void HttpParser::copy_param_map(STRING_MAP& param_map)
{
    param_map = param_map_;
}

void HttpParser::copy_header_map(STRING_MAP& header_map)
{
    header_map = header_map_;
}

char* HttpParser::get_line(char* data, uint32_t len, uint32_t& pos)
{
    bool b_line = false;
    char *p_line = nullptr;
    
    uint32_t n_pos = pos;
    while (n_pos < len && !(b_line = data[n_pos++] == LF)) ;
    
    if (b_line) {
        p_line = (char*)(data + pos);
        data[n_pos - 1] = 0;
        if (n_pos - pos >= 2 && data[n_pos - 2] == CR) {
            data[n_pos - 2] = 0;
        }
        pos = n_pos;
    }
    
    return p_line;
}

KUMA_NS_END
