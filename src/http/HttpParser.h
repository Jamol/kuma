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
    typedef std::function<void(const char* name, const char* value)> EnumrateCallback;
    
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
    void reset();
    
    bool isRequest() { return is_request_; }
    bool headerComplete() { return header_complete_; }
    bool complete();
    bool error();
    
    uint32_t getStatusCode() { return status_code_; }
    const char* getLocation() { return getHeaderValue("Location"); }
    const char* getUrl() { return url_.c_str(); }
    const char* getUrlPath() { return url_path_.c_str(); }
    const char* getMethod() { return method_.c_str(); }
    const char* getVersion() { return version_.c_str(); }
    const char* getParamValue(const char* name);
    const char* getHeaderValue(const char* name);
    
    void forEachParam(EnumrateCallback cb);
    void forEachHeader(EnumrateCallback cb);
    
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
    ParseState parseHttp(uint8_t* data, uint32_t len, uint32_t& used_len);
    bool parseStatusLine(char* cur_line);
    bool parseHeaderLine(char* cur_line);
    ParseState parseChunk(uint8_t* data, uint32_t len, uint32_t& pos);
    char* getLine(char* data, uint32_t len, uint32_t& pos);
    
    bool decodeUrl();
    bool parseUrl();
    void addParamValue(const std::string& name, const std::string& value);
    void addHeaderValue(std::string& name, std::string& value);
    
    bool hasBody();
    
    void onHeaderComplete();
    void onComplete();
    
    int saveData(uint8_t* data, uint32_t len);
    void clearStrBuf() { str_buf_.clear(); }
    
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
