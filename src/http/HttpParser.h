#ifndef __HttpParser_H__
#define __HttpParser_H__

#include "kmdefs.h"
#include "util/util.h"
#include <string>
#include <map>
#include <vector>
#include <functional>

KUMA_NS_BEGIN

class HttpParser
{
public:
    typedef enum {
        HTTP_HEADER_COMPLETE,
        HTTP_COMPLETE,
        HTTP_ERROR
    }HttpEvent;
    
    typedef std::function<void(const char*, uint32_t)> DataCallback;
    typedef std::function<void(HttpEvent)> EventCallback;
    typedef std::function<void(const std::string& name, const std::string& value)> EnumrateCallback;
    
    struct CaseIgnoreLess : public std::binary_function<std::string, std::string, bool> {
        bool operator()(const std::string &lhs, const std::string &rhs) const {
            return strcasecmp(lhs.c_str(), rhs.c_str()) < 0;
        }
    };
    typedef std::map<std::string, std::string, CaseIgnoreLess> STRING_MAP;
    
    HttpParser();
    ~HttpParser();
    
    // return bytes parsed
    int parse(const char* data, uint32_t len);
    // true - http completed
    bool setEOF();
    void reset();
    
    bool isRequest() { return is_request_; }
    bool headerComplete() { return header_complete_; }
    bool complete();
    bool error();
    
    int getStatusCode() { return status_code_; }
    const std::string& getLocation() { return getHeaderValue("Location"); }
    const std::string& getUrl() { return url_; }
    const std::string& getUrlPath() { return url_path_; }
    const std::string& getMethod() { return method_; }
    const std::string& getVersion() { return version_; }
    const std::string& getParamValue(const std::string& name);
    const std::string& getHeaderValue(const std::string& name);
    
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
    ParseState parseHttp(const char*& cur_pos, const char* end);
    bool parseStartLine(const char* line, const char* line_end);
    bool parseHeaderLine(const char* line, const char* line_end);
    ParseState parseChunk(const char*& cur_pos, const char* end);
    bool getLine(const char*& cur_pos, const char* end, const char*& line, const char*& line_end);
    
    bool decodeUrl();
    bool parseUrl();
    void addParamValue(const std::string& name, const std::string& value);
    void addHeaderValue(std::string& name, std::string& value);
    
    bool hasBody();
    bool readEOF();
    
    void onHeaderComplete();
    void onComplete();
    
    int saveData(const char* cur_pos, const char* end);
    bool bufferEmpty() { return str_buf_.empty(); };
    void clearBuffer() { str_buf_.clear(); }
    
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
    bool                upgrade_;
    
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
