#ifndef __HttpParser_H__
#define __HttpParser_H__

#include "kmdefs.h"
#include <string>
#include <map>
#include <vector>

KUMA_NS_BEGIN

class HttpParser
{
public:
    typedef enum {
        HTTP_HEADER_COMPLETE,
        HTTP_COMPLETE,
        HTTP_ERROR
    }HttpEvent;
    
    typedef std::function<void(uint8_t*, uint32_t)> DataCallback;
    typedef std::function<void(HttpEvent)> EventCallback;
    
    struct CaseIgnoreLess : public std::binary_function<std::string, std::string, bool> {
        bool operator()(const std::string &lhs, const std::string &rhs) const {
            return strcasecmp(lhs.c_str(), rhs.c_str()) < 0;
        }
    };
    typedef std::map<std::string, std::string, CaseIgnoreLess> STRING_MAP;
    
    HttpParser();
    ~HttpParser();
    
    // return bytes parsed
    int parse(uint8_t* data, uint32_t len);
    
    bool header_complete() { return header_complete_; }
    bool complete();
    bool error();
    uint32_t get_status_code() { return status_code_; }
    const char* get_location() { return get_header_value("Location"); }
    bool is_request() { return is_request_; }
    const char* get_uri() { return url_.c_str(); }
    const char* get_url_path() { return url_path_.c_str(); }
    const char* get_method() { return method_.c_str(); }
    const char* get_version() { return version_.c_str(); }
    const char* get_param_value(const char* name);
    const char* get_header_value(const char* name);
    void set_request(bool is_req) { is_request_ = is_req; }
    void reset();
    
    void copy_param_map(STRING_MAP& param_map);
    void copy_header_map(STRING_MAP& header_map);
    
    void setDataCallback(DataCallback& cb) { cb_data_ = cb; }
    void setEventCallback(EventCallback& cb) { cb_event_ = cb; }
    void setDataCallback(DataCallback&& cb) { cb_data_ = std::move(cb); }
    void setEventCallback(EventCallback&& cb) { cb_event_ = std::move(cb); }
    
private:
    typedef enum{
        PARSE_STATE_CONTINUE,
        PARSE_STATE_DONE,
        PARSE_STATE_ERROR,
        PARSE_STATE_DESTROY
    }ParseState;
    
private:
    ParseState parse_http(uint8_t* data, uint32_t len, uint32_t& used_len);
    bool parse_status_line(char* cur_line);
    bool parse_header_line(char* cur_line);
    ParseState parse_chunk(uint8_t* data, uint32_t len, uint32_t& pos);
    char* get_line(char* data, uint32_t len, uint32_t& pos);
    
    bool decode_url();
    bool parse_url();
    void add_param_value(const std::string& name, const std::string& value);
    void add_header_value(std::string& name, std::string& value);
    
    void on_header_complete();
    void on_complete();
    
    int save_data(uint8_t* data, uint32_t len);
    void clear_buffer() { str_buf_.clear(); }
    
private:
    HttpParser &operator= (const HttpParser &);
    HttpParser(const HttpParser &);
    
private:
    DataCallback        cb_data_;
    EventCallback       cb_event_;
    bool                is_request_;
    
    std::string         str_buf_;
    
    int                 read_state_;
    bool                header_complete_;
    
    bool                has_content_length_;
    uint32_t            content_length_;
    
    bool                is_chunked_;
    uint32_t            chunk_state_;
    uint32_t            chunk_size_;
    uint32_t            chunk_bytes_read_;
    
    uint32_t            total_bytes_read_;
    
    // request
    std::string         method_;
    std::string         url_;
    std::string         version_;
    std::string         url_path_;
    STRING_MAP          param_map_;
    STRING_MAP          header_map_;
    
    // response
    int                 status_code_;
    
    bool*               destroy_flag_ptr_;
};

KUMA_NS_END

#endif
