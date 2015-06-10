#ifndef __HttpResponseImpl_H__
#define __HttpResponseImpl_H__

#include "kmdefs.h"
#include "HttpParser.h"
#include "TcpSocketImpl.h"
#include "Uri.h"

KUMA_NS_BEGIN

class HttpResponseImpl
{
public:
    typedef std::function<void(uint8_t*, uint32_t)> DataCallback;
    typedef std::function<void(int)> EventCallback;
    typedef std::function<void(void)> HttpEventCallback;
    
    HttpResponseImpl(EventLoopImpl* loop);
    ~HttpResponseImpl();
    
    int attachFd(SOCKET_FD fd, uint8_t* init_data = nullptr, uint32_t init_len = 0);
    void addHeader(const char* name, const char* value);
    void addHeader(const char* name, uint32_t value);
    int sendResponse(int status_code, const char* desc = nullptr, const char* ver = "HTTP/1.1");
    int sendData(uint8_t* data, uint32_t len);
    int close();
    
    const char* getMethod() { return http_parser_.getMethod(); }
    const char* getUrl() { return http_parser_.getUrl(); }
    const char* getVersion() { return http_parser_.getVersion(); }
    const char* getParamValue(const char* name) { return http_parser_.getParamValue(name); }
    const char* getHeaderValue(const char* name) { return http_parser_.getHeaderValue(name); }
    void forEachHeader(HttpParser::EnumrateCallback cb) { return http_parser_.forEachHeader(cb); }
    
    void setDataCallback(DataCallback& cb) { cb_data_ = cb; }
    void setWriteCallback(EventCallback& cb) { cb_write_ = cb; }
    void setErrorCallback(EventCallback& cb) { cb_error_ = cb; }
    void setHeaderCompleteCallback(HttpEventCallback& cb) { cb_header_ = cb; }
    void setRequestCompleteCallback(HttpEventCallback& cb) { cb_request_ = cb; }
    void setResponseCompleteCallback(HttpEventCallback& cb) { cb_response_ = cb; }
    void setDataCallback(DataCallback&& cb) { cb_data_ = std::move(cb); }
    void setWriteCallback(EventCallback&& cb) { cb_write_ = std::move(cb); }
    void setErrorCallback(EventCallback&& cb) { cb_error_ = std::move(cb); }
    void setHeaderCompleteCallback(HttpEventCallback&& cb) { cb_header_ = std::move(cb); }
    void setRequestCompleteCallback(HttpEventCallback&& cb) { cb_request_ = std::move(cb); }
    void setResponseCompleteCallback(HttpEventCallback&& cb) { cb_response_ = std::move(cb); }

protected: // callbacks of tcp_socket
    void onSend(int err);
    void onReceive(int err);
    void onClose(int err);
    
protected:
    const char* getObjKey();
    
private:
    enum State {
        STATE_IDLE,
        STATE_RECVING_REQUEST,
        STATE_SENDING_RESPONSE,
        STATE_COMPLETE,
        STATE_ERROR,
        STATE_CLOSED
    };
    void setState(State state) { state_ = state; }
    State getState() { return state_; }
    void buildResponse(int status_code, const char* desc, const char* ver);
    int sendChunk(uint8_t* data, uint32_t len);
    void cleanup();
    
    void onHttpData(const char* data, uint32_t len);
    void onHttpEvent(HttpParser::HttpEvent ev);
    void notifyComplete();
    
private:
    HttpParser              http_parser_;
    State                   state_;
    EventLoopImpl*          loop_;
    
    std::vector<uint8_t>    send_buffer_;
    uint32_t                send_offset_;
    TcpSocketImpl           tcp_socket_;
    
    HttpParser::STRING_MAP  header_map_;
    
    bool                    is_chunked_;
    bool                    has_content_length_;
    uint32_t                content_length_;
    uint32_t                body_bytes_sent_;
    
    DataCallback            cb_data_;
    EventCallback           cb_write_;
    EventCallback           cb_error_;
    HttpEventCallback       cb_header_;
    HttpEventCallback       cb_request_;
    HttpEventCallback       cb_response_;
    
    bool*                   destroy_flag_ptr_;
};

KUMA_NS_END

#endif
