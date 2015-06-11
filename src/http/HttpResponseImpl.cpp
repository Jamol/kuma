
#include "HttpResponseImpl.h"
#include "EventLoopImpl.h"
#include "util/kmtrace.h"

KUMA_NS_BEGIN

//////////////////////////////////////////////////////////////////////////
HttpResponseImpl::HttpResponseImpl(EventLoopImpl* loop)
: http_parser_()
, state_(STATE_IDLE)
, loop_(loop)
, send_offset_(0)
, tcp_socket_(loop)
, is_chunked_(false)
, has_content_length_(false)
, content_length_(0)
, body_bytes_sent_(0)
, destroy_flag_ptr_(nullptr)
{

}

HttpResponseImpl::~HttpResponseImpl()
{
    if(destroy_flag_ptr_) {
        *destroy_flag_ptr_ = true;
    }
}

const char* HttpResponseImpl::getObjKey()
{
    return "HttpResponse";
}

void HttpResponseImpl::cleanup()
{
    tcp_socket_.close();
    send_buffer_.clear();
    send_offset_ = 0;
}

int HttpResponseImpl::attachFd(SOCKET_FD fd, uint8_t* init_data, uint32_t init_len)
{
    http_parser_.reset();
    http_parser_.setDataCallback([this] (const char* data, uint32_t len) { onHttpData(data, len); });
    http_parser_.setEventCallback([this] (HttpParser::HttpEvent ev) { onHttpEvent(ev); });
    tcp_socket_.setReadCallback([this] (int err) { onReceive(err); });
    tcp_socket_.setWriteCallback([this] (int err) { onSend(err); });
    tcp_socket_.setErrorCallback([this] (int err) { onClose(err); });
    return tcp_socket_.attachFd(fd, 0, init_data, init_len);
}

void HttpResponseImpl::addHeader(const char* name, const char* value)
{
    if(name && name[0] != '\0') {
        header_map_[name] = value;
    }
}

void HttpResponseImpl::addHeader(const char* name, uint32_t value)
{
    std::stringstream ss;
    ss << value;
    addHeader(name, ss.str().c_str());
}

void HttpResponseImpl::buildResponse(int status_code, const char* desc, const char* ver)
{
    std::stringstream ss;
    ss << (ver?ver:"HTTP/1.1") << " " << status_code << " " << (desc?desc:"") << "\r\n";
    if(header_map_.find("Content-Type") == header_map_.end()) {
        ss << "Content-Type: application/octet-stream\r\n";
    }
    for (auto &kv : header_map_) {
        ss << kv.first << ": " << kv.second << "\r\n";
    }
    ss << "\r\n";
    std::string str(ss.str());
    send_buffer_.clear();
    send_offset_ = 0;
    send_buffer_.reserve(str.length());
    send_buffer_.insert(send_buffer_.end(), str.begin(), str.end());
}

int HttpResponseImpl::sendResponse(int status_code, const char* desc, const char* ver)
{
    KUMA_INFOXTRACE("sendResponse, status_code="<<status_code);
    auto it = header_map_.find("Content-Length");
    if(it != header_map_.end()) {
        has_content_length_ = true;
        content_length_ = atoi(it->second.c_str());
    }
    it = header_map_.find("Transfer-Encoding");
    if(it != header_map_.end() && is_equal("chunked", it->second)) {
        is_chunked_ = true;
    }
    body_bytes_sent_ = 0;
    buildResponse(status_code, desc, ver);
    setState(STATE_SENDING_RESPONSE);
    int ret = tcp_socket_.send(&send_buffer_[0] + send_offset_, (uint32_t)send_buffer_.size() - send_offset_);
    if(ret < 0) {
        cleanup();
        setState(STATE_ERROR);
        return KUMA_ERROR_SOCKERR;
    } else {
        send_offset_ += ret;
        if(send_offset_ == send_buffer_.size()) {
            send_offset_ = 0;
            send_buffer_.clear();
            if(has_content_length_ && 0 == content_length_ && !is_chunked_) {
                setState(STATE_COMPLETE);
                loop_->queueInEventLoop([this] { notifyComplete(); });
            }
        }
    }
    return KUMA_ERROR_NOERR;
}

int HttpResponseImpl::sendData(uint8_t* data, uint32_t len)
{
    if(!send_buffer_.empty() || getState() != STATE_SENDING_RESPONSE) {
        return 0;
    }
    if(is_chunked_) {
        return sendChunk(data, len);
    }
    if(!data || 0 == len) {
        return 0;
    }
    int ret = tcp_socket_.send(data, len);
    if(ret < 0) {
        setState(STATE_ERROR);
    } else if(ret > 0 && has_content_length_ && body_bytes_sent_ >= content_length_) {
        setState(STATE_COMPLETE);
        loop_->queueInEventLoop([this] { notifyComplete(); });
    }
    return ret;
}

int HttpResponseImpl::sendChunk(uint8_t* data, uint32_t len)
{
    if(nullptr == data && 0 == len) { // chunk end
        static const std::string _chunk_end_token_ = "0\r\n\r\n";
        int ret = tcp_socket_.send((uint8_t*)_chunk_end_token_.c_str(), (uint32_t)_chunk_end_token_.length());
        if(ret < 0) {
            setState(STATE_ERROR);
            return ret;
        } else if(ret < 5) {
            std::copy(_chunk_end_token_.begin() + ret, _chunk_end_token_.end(), back_inserter(send_buffer_));
            send_offset_ = 0;
        }
        return 0;
    } else {
        std::stringstream ss;
        ss.setf(std::ios_base::hex, std::ios_base::basefield);
        ss << len << "\r\n";
        std::string str;
        ss >> str;
        iovec iovs[3];
        iovs[0].iov_base = (char*)str.c_str();
        iovs[0].iov_len = str.length();
        iovs[1].iov_base = (char*)data;
        iovs[1].iov_len = len;
        iovs[2].iov_base = (char*)"\r\n";
        iovs[2].iov_len = 2;
        auto total_len = iovs[0].iov_len + iovs[1].iov_len + iovs[2].iov_len;
        int ret = tcp_socket_.send(iovs, 3);
        if(ret < 0) {
            return ret;
        } else if(ret < total_len) {
            send_buffer_.reserve(total_len - ret);
            send_offset_ = 0;
            for (auto &iov : iovs) {
                uint8_t* first = ((uint8_t*)iov.iov_base) + ret;
                uint8_t* last = ((uint8_t*)iov.iov_base) + iov.iov_len;
                if(first < last) {
                    send_buffer_.insert(send_buffer_.end(), first, last);
                    ret = 0;
                } else {
                    ret -= iov.iov_len;
                }
            }
        }
        return len;
    }
}

int HttpResponseImpl::close()
{
    KUMA_INFOXTRACE("close");
    cleanup();
    setState(STATE_CLOSED);
    return KUMA_ERROR_NOERR;
}

void HttpResponseImpl::onSend(int err)
{
    if(!send_buffer_.empty() && send_offset_ < send_buffer_.size()) {
        int ret = tcp_socket_.send(&send_buffer_[0] + send_offset_, (uint32_t)send_buffer_.size() - send_offset_);
        if(ret < 0) {
            cleanup();
            setState(STATE_ERROR);
            if(cb_error_) cb_error_(KUMA_ERROR_SOCKERR);
            return;
        } else {
            send_offset_ += ret;
            if(send_offset_ == send_buffer_.size()) {
                send_offset_ = 0;
                send_buffer_.clear();
                if(!is_chunked_ && body_bytes_sent_ >= content_length_) {
                    setState(STATE_COMPLETE);
                    if(cb_response_) cb_response_();
                    return ;
                }
            }
        }
    }
    if(send_buffer_.empty() && cb_write_) {
        cb_write_(0);
    }
}

void HttpResponseImpl::onReceive(int err)
{
    char buf[256*1024];
    do {
        int ret = tcp_socket_.receive((uint8_t*)buf, sizeof(buf));
        if(ret < 0) {
            cleanup();
            setState(STATE_ERROR);
            if(cb_error_) cb_error_(KUMA_ERROR_SOCKERR);
        } else if(0 == ret) {
            break;
        } else {
            bool destroyed = false;
            KUMA_ASSERT(nullptr == destroy_flag_ptr_);
            destroy_flag_ptr_ = &destroyed;
            int bytes_used = http_parser_.parse(buf, ret);
            if(destroyed) {
                return;
            }
            destroy_flag_ptr_ = nullptr;
            if(getState() == STATE_ERROR || getState() == STATE_CLOSED) {
                break;
            }
            if(bytes_used != ret) {
                KUMA_WARNXTRACE("onReceive, bytes_used="<<bytes_used<<", bytes_read="<<ret);
            }
        }
    } while(true);
}

void HttpResponseImpl::onClose(int err)
{
    KUMA_INFOXTRACE("onClose, err="<<err);
    cleanup();
    if(getState() < STATE_COMPLETE) {
        setState(STATE_ERROR);
        if(cb_error_) cb_error_(KUMA_ERROR_SOCKERR);
    } else {
        setState(STATE_CLOSED);
    }
}

void HttpResponseImpl::onHttpData(const char* data, uint32_t len)
{
    if(cb_data_) cb_data_((uint8_t*)data, len);
}

void HttpResponseImpl::onHttpEvent(HttpParser::HttpEvent ev)
{
    KUMA_INFOXTRACE("onHttpEvent, ev="<<ev);
    switch (ev) {
        case HttpParser::HTTP_HEADER_COMPLETE:
            if(cb_header_) cb_header_();
            break;
            
        case HttpParser::HTTP_COMPLETE:
            setState(STATE_SENDING_RESPONSE);
            if(cb_request_) cb_request_();
            break;
            
        case HttpParser::HTTP_ERROR:
            cleanup();
            setState(STATE_ERROR);
            if(cb_error_) cb_error_(KUMA_ERROR_FAILED);
            break;
            
        default:
            break;
    }
}

void HttpResponseImpl::notifyComplete()
{
    if(cb_response_) cb_response_();
}

KUMA_NS_END
