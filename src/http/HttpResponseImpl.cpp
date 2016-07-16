/* Copyright (c) 2014, Fengping Bao <jamol@live.com>
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include "HttpResponseImpl.h"
#include "EventLoopImpl.h"
#include "util/kmtrace.h"

#include <iterator>

using namespace kuma;

static const std::string str_content_type = "Content-Type";
static const std::string str_content_length = "Content-Length";
static const std::string str_transfer_encoding = "Transfer-Encoding";
static const std::string str_chunked = "chunked";
//////////////////////////////////////////////////////////////////////////
HttpResponseImpl::HttpResponseImpl(EventLoopImpl* loop)
: http_parser_()
, state_(STATE_IDLE)
, loop_(loop)
, init_data_(nullptr)
, init_len_(0)
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
    if(init_data_) {
        delete [] init_data_;
        init_data_ = nullptr;
        init_len_ = 0;
    }
}

const char* HttpResponseImpl::getObjKey() const
{
    return "HttpResponse";
}

void HttpResponseImpl::cleanup()
{
    tcp_socket_.close();
    send_buffer_.clear();
    send_offset_ = 0;
}

int HttpResponseImpl::setSslFlags(uint32_t ssl_flags)
{
    return tcp_socket_.setSslFlags(ssl_flags);
}

int HttpResponseImpl::attachFd(SOCKET_FD fd, uint8_t* init_data, size_t init_len)
{
    if(init_data && init_len > 0) {
        init_data = new uint8_t(init_len);
        memcpy(init_data_, init_data, init_len);
        init_len_ = init_len;
    }
    setState(STATE_RECVING_REQUEST);
    http_parser_.reset();
    http_parser_.setDataCallback([this] (const char* data, size_t len) { onHttpData(data, len); });
    http_parser_.setEventCallback([this] (HttpEvent ev) { onHttpEvent(ev); });
    tcp_socket_.setReadCallback([this] (int err) { onReceive(err); });
    tcp_socket_.setWriteCallback([this] (int err) { onSend(err); });
    tcp_socket_.setErrorCallback([this] (int err) { onClose(err); });
    return tcp_socket_.attachFd(fd);
}

int HttpResponseImpl::attachSocket(TcpSocketImpl&& tcp, HttpParserImpl&& parser)
{
    setState(STATE_RECVING_REQUEST);
    http_parser_.reset();
    http_parser_ = std::move(parser);
    http_parser_.setDataCallback([this] (const char* data, size_t len) { onHttpData(data, len); });
    http_parser_.setEventCallback([this] (HttpEvent ev) { onHttpEvent(ev); });
    tcp_socket_.setReadCallback([this] (int err) { onReceive(err); });
    tcp_socket_.setWriteCallback([this] (int err) { onSend(err); });
    tcp_socket_.setErrorCallback([this] (int err) { onClose(err); });
#ifdef KUMA_HAS_OPENSSL
    SOCKET_FD fd;
    SSL* ssl = nullptr;
    uint32_t ssl_flags = tcp.getSslFlags();
    int ret = tcp.detachFd(fd, ssl);
    tcp_socket_.setSslFlags(ssl_flags);
    ret = tcp_socket_.attachFd(fd, ssl);
#else
    SOCKET_FD fd;
    int ret = tcp.detachFd(fd);
    ret = tcp_socket_.attachFd(fd);
#endif
    if(http_parser_.paused()) {
        http_parser_.resume();
    }
    return ret;
}

void HttpResponseImpl::addHeader(const std::string& name, const std::string& value)
{
    if(!name.empty()) {
        header_map_[name] = value;
    }
}

void HttpResponseImpl::addHeader(const std::string& name, uint32_t value)
{
    std::stringstream ss;
    ss << value;
    addHeader(name, ss.str());
}

void HttpResponseImpl::buildResponse(int status_code, const std::string& desc, const std::string& ver)
{
    std::stringstream ss;
    ss << (!ver.empty()?ver:VersionHTTP1_1) << " " << status_code << " " << desc << "\r\n";
    if(header_map_.find(str_content_type) == header_map_.end()) {
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

int HttpResponseImpl::sendResponse(int status_code, const std::string& desc, const std::string& ver)
{
    KUMA_INFOXTRACE("sendResponse, status_code="<<status_code);
    if (getState() != STATE_WAIT_FOR_RESPONSE) {
        return KUMA_ERROR_INVALID_STATE;
    }
    auto it = header_map_.find(str_content_length);
    if(it != header_map_.end()) {
        has_content_length_ = true;
        content_length_ = atoi(it->second.c_str());
    }
    it = header_map_.find(str_transfer_encoding);
    if(it != header_map_.end() && is_equal(str_chunked, it->second)) {
        is_chunked_ = true;
    }
    body_bytes_sent_ = 0;
    buildResponse(status_code, desc, ver);
    setState(STATE_SENDING_HEADER);
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
            } else {
                setState(STATE_SENDING_BODY);
                loop_->queueInEventLoop([this] { if (cb_write_) cb_write_(0); });
            }
        }
    }
    return KUMA_ERROR_NOERR;
}

int HttpResponseImpl::sendData(const uint8_t* data, size_t len)
{
    if(!send_buffer_.empty() || getState() != STATE_SENDING_BODY) {
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
    } else if(ret > 0) {
        body_bytes_sent_ += ret;
        if (has_content_length_ && body_bytes_sent_ >= content_length_) {
            setState(STATE_COMPLETE);
            loop_->queueInEventLoop([this] { notifyComplete(); });
        }
    }
    return ret;
}

int HttpResponseImpl::sendChunk(const uint8_t* data, size_t len)
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
        } else {
            setState(STATE_COMPLETE);
            loop_->queueInEventLoop([this] { notifyComplete(); });
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
        return (int)len;
    }
}

void HttpResponseImpl::reset()
{
    http_parser_.reset();
    header_map_.clear();
    send_buffer_.clear();
    send_offset_ = 0;
    has_content_length_ = false;
    content_length_ = 0;
    body_bytes_sent_ = 0;
    is_chunked_ = false;
    setState(STATE_RECVING_REQUEST);
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
                if (getState() == STATE_SENDING_HEADER) {
                    if(has_content_length_ && 0 == content_length_ && !is_chunked_) {
                        setState(STATE_COMPLETE);
                        notifyComplete();
                        return;
                    } else {
                        setState(STATE_SENDING_BODY);
                    }
                } else if (getState() == STATE_SENDING_BODY) {
                    body_bytes_sent_ += ret;
                    if(!is_chunked_ && has_content_length_ && body_bytes_sent_ >= content_length_) {
                        setState(STATE_COMPLETE);
                        notifyComplete();
                        return ;
                    }
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
    if(init_data_ && init_len_ > 0) {
        bool destroyed = false;
        KUMA_ASSERT(nullptr == destroy_flag_ptr_);
        destroy_flag_ptr_ = &destroyed;
        int bytes_used = http_parser_.parse((char*)init_data_, init_len_);
        if(destroyed) {
            return;
        }
        destroy_flag_ptr_ = nullptr;
        if(getState() == STATE_ERROR || getState() == STATE_CLOSED) {
            return;
        }
        if(bytes_used != init_len_) {
            KUMA_WARNXTRACE("onReceive, bytes_used="<<bytes_used<<", init_len="<<init_len_);
            memmove(init_data_, init_data_ + bytes_used, init_len_ - bytes_used);
            init_len_ -= bytes_used;
            return;
        }
        delete [] init_data_;
        init_data_ = nullptr;
        init_len_ = 0;
    }
    char buf[128*1024];
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

void HttpResponseImpl::onHttpData(const char* data, size_t len)
{
    if(cb_data_) cb_data_((uint8_t*)data, len);
}

void HttpResponseImpl::onHttpEvent(HttpEvent ev)
{
    KUMA_INFOXTRACE("onHttpEvent, ev="<<ev);
    switch (ev) {
        case HTTP_HEADER_COMPLETE:
            if(cb_header_) cb_header_();
            break;
            
        case HTTP_COMPLETE:
            setState(STATE_WAIT_FOR_RESPONSE);
            if(cb_request_) cb_request_();
            break;
            
        case HTTP_ERROR:
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
