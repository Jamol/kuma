/* Copyright (c) 2014, Fengping Bao <jamol@live.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include "Http1xResponse.h"
#include "EventLoopImpl.h"
#include "util/kmtrace.h"

#include <iterator>

using namespace kuma;

static const std::string str_content_type = "Content-Type";
static const std::string str_content_length = "Content-Length";
static const std::string str_transfer_encoding = "Transfer-Encoding";
static const std::string str_chunked = "chunked";
//////////////////////////////////////////////////////////////////////////
Http1xResponse::Http1xResponse(EventLoop::Impl* loop, std::string ver)
: HttpResponse::Impl(std::move(ver)), TcpConnection(loop)
{
    KM_SetObjKey("Http1xResponse");
}

Http1xResponse::~Http1xResponse()
{
    
}

void Http1xResponse::cleanup()
{
    TcpConnection::close();
}

KMError Http1xResponse::setSslFlags(uint32_t ssl_flags)
{
    return TcpConnection::setSslFlags(ssl_flags);
}

KMError Http1xResponse::attachFd(SOCKET_FD fd, uint8_t* init_data, size_t init_len)
{
    setState(State::RECVING_REQUEST);
    http_parser_.reset();
    http_parser_.setDataCallback([this] (const char* data, size_t len) { onHttpData(data, len); });
    http_parser_.setEventCallback([this] (HttpEvent ev) { onHttpEvent(ev); });
    return TcpConnection::attachFd(fd);
}

KMError Http1xResponse::attachSocket(TcpSocket::Impl&& tcp, HttpParser::Impl&& parser)
{
    setState(State::RECVING_REQUEST);
    http_parser_.reset();
    http_parser_ = std::move(parser);
    http_parser_.setDataCallback([this] (const char* data, size_t len) { onHttpData(data, len); });
    http_parser_.setEventCallback([this] (HttpEvent ev) { onHttpEvent(ev); });

    auto ret = TcpConnection::attachSocket(std::move(tcp));
    if(ret == KMError::NOERR && http_parser_.paused()) {
        http_parser_.resume();
    }
    return ret;
}

void Http1xResponse::buildResponse(int status_code, const std::string& desc, const std::string& ver)
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
    send_offset_ = 0;
    send_buffer_.assign(str.begin(), str.end());
}

KMError Http1xResponse::sendResponse(int status_code, const std::string& desc, const std::string& ver)
{
    KUMA_INFOXTRACE("sendResponse, status_code="<<status_code);
    if (getState() != State::WAIT_FOR_RESPONSE) {
        return KMError::INVALID_STATE;
    }
    auto it = header_map_.find(str_content_length);
    if(it != header_map_.end()) {
        has_content_length_ = true;
        content_length_ = std::stoi(it->second);
    }
    it = header_map_.find(str_transfer_encoding);
    if(it != header_map_.end() && is_equal(str_chunked, it->second)) {
        is_chunked_ = true;
    }
    body_bytes_sent_ = 0;
    buildResponse(status_code, desc, ver);
    setState(State::SENDING_HEADER);
    auto ret = sendBufferedData();
    if(ret != KMError::NOERR) {
        cleanup();
        setState(State::IN_ERROR);
        return KMError::SOCK_ERROR;
    } else if (sendBufferEmpty()) {
        if(has_content_length_ && 0 == content_length_ && !is_chunked_) {
            setState(State::COMPLETE);
            getEventLoop()->queueInEventLoop([this] { notifyComplete(); });
        } else {
            setState(State::SENDING_BODY);
            getEventLoop()->queueInEventLoop([this] { if (write_cb_) write_cb_(KMError::NOERR); });
        }
    }
    return KMError::NOERR;
}

int Http1xResponse::sendData(const uint8_t* data, size_t len)
{
    if(!sendBufferEmpty() || getState() != State::SENDING_BODY) {
        return 0;
    }
    if(is_chunked_) {
        return sendChunk(data, len);
    }
    if(!data || 0 == len) {
        return 0;
    }
    int ret = TcpConnection::send(data, len);
    if(ret < 0) {
        setState(State::IN_ERROR);
    } else if(ret > 0) {
        body_bytes_sent_ += ret;
        if (has_content_length_ && body_bytes_sent_ >= content_length_ && sendBufferEmpty()) {
            setState(State::COMPLETE);
            getEventLoop()->queueInEventLoop([this] { notifyComplete(); });
        }
    }
    return ret;
}

int Http1xResponse::sendChunk(const uint8_t* data, size_t len)
{
    if(nullptr == data && 0 == len) { // chunk end
        static const std::string _chunk_end_token_ = "0\r\n\r\n";
        int ret = TcpConnection::send((uint8_t*)_chunk_end_token_.c_str(), (uint32_t)_chunk_end_token_.length());
        if(ret < 0) {
            setState(State::IN_ERROR);
            return ret;
        } else if(sendBufferEmpty()) { // should always empty
            setState(State::COMPLETE);
            getEventLoop()->queueInEventLoop([this] { notifyComplete(); });
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
        int ret = TcpConnection::send(iovs, 3);
        if(ret < 0) {
            return ret;
        }
        return (int)len;
    }
}

void Http1xResponse::reset()
{
    // reset TcpConnection
    send_buffer_.clear();
    send_offset_ = 0;
    
    HttpResponse::Impl::reset();
    setState(State::RECVING_REQUEST);
}

KMError Http1xResponse::close()
{
    KUMA_INFOXTRACE("close");
    cleanup();
    setState(State::CLOSED);
    return KMError::NOERR;
}

KMError Http1xResponse::handleInputData(uint8_t *src, size_t len)
{
    DESTROY_DETECTOR_SETUP();
    int bytes_used = http_parser_.parse((char*)src, len);
    DESTROY_DETECTOR_CHECK(KMError::DESTROYED);
    if(getState() == State::IN_ERROR || getState() == State::CLOSED) {
        return KMError::FAILED;
    }
    if(bytes_used != len) {
        KUMA_WARNXTRACE("handleInputData, bytes_used="<<bytes_used<<", bytes_read="<<len);
    }
    return KMError::NOERR;
}

void Http1xResponse::onWrite()
{
    if (getState() == State::SENDING_HEADER) {
        if(has_content_length_ && 0 == content_length_ && !is_chunked_) {
            setState(State::COMPLETE);
            notifyComplete();
            return;
        } else {
            setState(State::SENDING_BODY);
        }
    } else if (getState() == State::SENDING_BODY) {
        if(!is_chunked_ && has_content_length_ && body_bytes_sent_ >= content_length_) {
            setState(State::COMPLETE);
            notifyComplete();
            return ;
        }
    }
    if(write_cb_) write_cb_(KMError::NOERR);
}

void Http1xResponse::onError(KMError err)
{
    KUMA_INFOXTRACE("onError, err="<<int(err));
    cleanup();
    if(getState() < State::COMPLETE) {
        setState(State::IN_ERROR);
        if(error_cb_) error_cb_(KMError::SOCK_ERROR);
    } else {
        setState(State::CLOSED);
    }
}

void Http1xResponse::onHttpData(const char* data, size_t len)
{
    if(data_cb_) data_cb_((uint8_t*)data, len);
}

void Http1xResponse::onHttpEvent(HttpEvent ev)
{
    KUMA_INFOXTRACE("onHttpEvent, ev="<<int(ev));
    switch (ev) {
        case HttpEvent::HEADER_COMPLETE:
            if(header_cb_) header_cb_();
            break;
            
        case HttpEvent::COMPLETE:
            setState(State::WAIT_FOR_RESPONSE);
            if(request_cb_) request_cb_();
            break;
            
        case HttpEvent::HTTP_ERROR:
            cleanup();
            setState(State::IN_ERROR);
            if(error_cb_) error_cb_(KMError::FAILED);
            break;
            
        default:
            break;
    }
}
