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

#include "HttpRequestImpl.h"
#include "util/kmtrace.h"
#include "util/util.h"

#include <sstream>
#include <iterator>

using namespace kuma;

//////////////////////////////////////////////////////////////////////////
HttpRequestImpl::HttpRequestImpl(EventLoopImpl* loop)
: http_parser_()
, state_(STATE_IDLE)
, send_offset_(0)
, tcp_socket_(loop)
, is_chunked_(false)
, has_content_length_(false)
, content_length_(0)
, body_bytes_sent_(0)
, destroy_flag_ptr_(nullptr)
{
    
}

HttpRequestImpl::~HttpRequestImpl()
{
    if(destroy_flag_ptr_) {
        *destroy_flag_ptr_ = true;
    }
}

const char* HttpRequestImpl::getObjKey() const
{
    return "HttpRequest";
}

void HttpRequestImpl::cleanup()
{
    tcp_socket_.close();
    send_buffer_.clear();
    send_offset_ = 0;
}

int HttpRequestImpl::setSslFlags(uint32_t ssl_flags)
{
    if (getState() == STATE_IDLE) {
        return tcp_socket_.setSslFlags(ssl_flags);
    }
    return KUMA_ERROR_INVALID_STATE;
}

void HttpRequestImpl::addHeader(const std::string& name, const std::string& value)
{
    if(!name.empty()) {
        header_map_[name] = value;
    }
}

void HttpRequestImpl::addHeader(const std::string& name, uint32_t value)
{
    std::stringstream ss;
    ss << value;
    addHeader(name, ss.str());
}

void HttpRequestImpl::buildRequest()
{
    std::stringstream ss;
    ss << method_ << " ";
    ss << uri_.getPath();
    if(!uri_.getQuery().empty()) {
        ss << "?" << uri_.getQuery();
    }
    if(!uri_.getFragment().empty()) {
        ss << "#" << uri_.getFragment();
    }
    ss << " ";
    ss << version_ << "\r\n";
    if(header_map_.find("Accept") == header_map_.end()) {
        ss << "Accept: */*\r\n";
    }
    if(header_map_.find("Content-Type") == header_map_.end()) {
        ss << "Content-Type: application/octet-stream\r\n";
    }
    if(header_map_.find("User-Agent") == header_map_.end()) {
        ss << "User-Agent: kuma\r\n";
    }
    ss << "Host: " << uri_.getHost() << "\r\n";
    if(header_map_.find("Cache-Control") == header_map_.end()) {
        ss << "Cache-Control: no-cache\r\n";
    }
    if(header_map_.find("Pragma") == header_map_.end()) {
        ss << "Pragma: no-cache\r\n";
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

int HttpRequestImpl::sendRequest(const std::string& method, const std::string& url, const std::string& ver)
{
    if (getState() != STATE_IDLE && getState() != STATE_WAIT_FOR_REUSE) {
        return KUMA_ERROR_INVALID_STATE;
    }
    method_ = method;
    url_ = url;
    version_ = ver;
    if(!uri_.parse(url)) {
        return KUMA_ERROR_INVALID_PARAM;
    }
    
    auto it = header_map_.find("Content-Length");
    if(it != header_map_.end()) {
        has_content_length_ = true;
        content_length_ = atoi(it->second.c_str());
    }
    it = header_map_.find("Transfer-Encoding");
    if(it != header_map_.end() && is_equal("chunked", it->second)) {
        is_chunked_ = true;
    }
    
    if (getState() == STATE_IDLE) {
        tcp_socket_.setReadCallback([this] (int err) { onReceive(err); });
        tcp_socket_.setWriteCallback([this] (int err) { onSend(err); });
        tcp_socket_.setErrorCallback([this] (int err) { onClose(err); });
        setState(STATE_CONNECTING);
        std::string str_port = uri_.getPort();
        uint16_t port = 80;
        uint32_t ssl_flags = SSL_NONE;
        if(is_equal("https", uri_.getScheme())) {
            port = 443;
            ssl_flags = SSL_ENABLE | tcp_socket_.getSslFlags();
        }
        if(!str_port.empty()) {
            port = atoi(str_port.c_str());
        }
        tcp_socket_.setSslFlags(ssl_flags);
        return tcp_socket_.connect(uri_.getHost().c_str(), port, [this] (int err) { onConnect(err); });
    } else { // connection reuse
        sendRequestHeader();
        return KUMA_ERROR_NOERR;
    }
}

int HttpRequestImpl::sendData(const uint8_t* data, uint32_t len)
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
        if (body_bytes_sent_ >= content_length_) {
            setState(STATE_RECVING_RESPONSE);
        }
    }
    return ret;
}

int HttpRequestImpl::sendChunk(const uint8_t* data, uint32_t len)
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
            setState(STATE_RECVING_RESPONSE);
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

void HttpRequestImpl::reset()
{
    http_parser_.reset();
    header_map_.clear();
    send_buffer_.clear();
    send_offset_ = 0;
    has_content_length_ = false;
    content_length_ = 0;
    body_bytes_sent_ = 0;
    is_chunked_ = false;
    if (getState() == STATE_COMPLETE) {
        setState(STATE_WAIT_FOR_REUSE);
    }
}

int HttpRequestImpl::close()
{
    KUMA_INFOXTRACE("close");
    cleanup();
    setState(STATE_CLOSED);
    return KUMA_ERROR_NOERR;
}

void HttpRequestImpl::sendRequestHeader()
{
    body_bytes_sent_ = 0;
    http_parser_.setDataCallback([this] (const char* data, uint32_t len) { onHttpData(data, len); });
    http_parser_.setEventCallback([this] (HttpEvent ev) { onHttpEvent(ev); });
    buildRequest();
    setState(STATE_SENDING_HEADER);
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
            if(!is_chunked_ && 0 == content_length_) {
                setState(STATE_RECVING_RESPONSE);
            } else {
                setState(STATE_SENDING_BODY);
                if (cb_write_) {
                    cb_write_(0);
                }
            }
        }
    }
}

void HttpRequestImpl::onConnect(int err)
{
    if(err != KUMA_ERROR_NOERR) {
        if(cb_error_) cb_error_(err);
        return ;
    }
    sendRequestHeader();
}

void HttpRequestImpl::onSend(int err)
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
                    if(!is_chunked_ && 0 == content_length_) {
                        setState(STATE_RECVING_RESPONSE);
                        return;
                    } else {
                        setState(STATE_SENDING_BODY);
                    }
                } else if (getState() == STATE_SENDING_BODY) {
                    body_bytes_sent_ += ret;
                    if (!is_chunked_ && body_bytes_sent_ >= content_length_) {
                        setState(STATE_RECVING_RESPONSE);
                        return;
                    }
                }
            }
        }
    }
    if(send_buffer_.empty() && cb_write_) {
        cb_write_(0);
    }
}

void HttpRequestImpl::onReceive(int err)
{
    char buf[128*1024];
    do {
        int ret = tcp_socket_.receive((uint8_t*)buf, sizeof(buf));
        if(ret < 0) {
            bool destroyed = false;
            KUMA_ASSERT(nullptr == destroy_flag_ptr_);
            destroy_flag_ptr_ = &destroyed;
            bool completed = http_parser_.setEOF();
            if(destroyed) {
                return;
            }
            destroy_flag_ptr_ = nullptr;
            if(completed) {
                cleanup();
            } else {
                cleanup();
                setState(STATE_ERROR);
                if(cb_error_) cb_error_(KUMA_ERROR_SOCKERR);
            }
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

void HttpRequestImpl::onClose(int err)
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

void HttpRequestImpl::onHttpData(const char* data, uint32_t len)
{
    if(cb_data_) cb_data_((uint8_t*)data, len);
}

void HttpRequestImpl::onHttpEvent(HttpEvent ev)
{
    KUMA_INFOXTRACE("onHttpEvent, ev="<<ev);
    switch (ev) {
        case HTTP_HEADER_COMPLETE:
            if(cb_header_) cb_header_();
            break;
            
        case HTTP_COMPLETE:
            setState(STATE_COMPLETE);
            if(cb_response_) cb_response_();
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
