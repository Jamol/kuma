#ifndef __HttpRequestImpl_H__
#define __HttpRequestImpl_H__

#include "kmdefs.h"
#include "HttpParser.h"
#include "TcpSocketImpl.h"
#include "Uri.h"

KUMA_NS_BEGIN

class HttpRequestImpl
{
public:
    typedef std::function<void(uint8_t*, uint32_t)> DataCallback;
    typedef std::function<void(int)> EventCallback;
    typedef std::function<void(void)> HeaderCompleteCallback;
    typedef std::function<void(void)> ResponseCompleteCallback;
    
    HttpRequestImpl(EventLoopImpl* loop);
    ~HttpRequestImpl();
    
    void addHeader(const char* name, const char* value);
    void addHeader(const char* name, uint32_t value);
    int sendRequest(const char* method, const char* uri, const char* ver);
    int sendData(uint8_t* data, uint32_t len);
    int close();
    
    void setDataCallback(DataCallback& cb) { cb_data_ = cb; }
    void setWriteCallback(EventCallback& cb) { cb_write_ = cb; }
    void setErrorCallback(EventCallback& cb) { cb_error_ = cb; }
    void setDataCallback(DataCallback&& cb) { cb_data_ = std::move(cb); }
    void setWriteCallback(EventCallback&& cb) { cb_write_ = std::move(cb); }
    void setErrorCallback(EventCallback&& cb) { cb_error_ = std::move(cb); }
    
protected:
    void onConnect(int err);
    void onSend(int err);
    void onReceive(int err);
    void onClose(int err);
    
protected:
    const char* getObjKey();

private:
    enum State {
        STATE_IDLE,
        STATE_CONNECTING,
        STATE_SENDING_REQUEST,
        STATE_RECVING_RESPONSE,
        STATE_COMPLETE,
        STATE_ERROR,
        STATE_CLOSED
    };
    void setState(State state) { state_ = state; }
    State getState() { return state_; }
    void buildRequest();
    int sendTrunk(uint8_t* data, uint32_t len);
    void cleanup();
    
    void onHttpData(uint8_t* data, uint32_t len);
    void onHttpEvent(HttpParser::HttpEvent ev);
    
private:
    HttpParser              http_parser_;
    State                   state_;
    HttpParser::STRING_MAP  header_map_;
    std::string             method_;
    std::string             url_;
    std::string             version_;
    Uri                     uri_;
    
    bool                    is_chunked_;
    bool                    has_content_length_;
    uint32_t                content_length_;
    uint32_t                body_bytes_sent_;
    
    std::vector<uint8_t>    send_buffer_;
    uint32_t                send_offset_;
    TcpSocketImpl           tcp_socket_;
    
    DataCallback            cb_data_;
    EventCallback           cb_write_;
    EventCallback           cb_error_;
    HeaderCompleteCallback  cb_header_;
    ResponseCompleteCallback    cb_response_;
    
    bool*                   destroy_flag_ptr_;
};

KUMA_NS_END

#endif
