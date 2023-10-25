/* Copyright (c) 2016-2019, Fengping Bao <jamol@live.com>
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

#include "H2StreamProxy.h"
#include "H2ConnectionMgr.h"
#include "libkev/src/utils/kmtrace.h"
#include "libkev/src/utils/utils.h"
#include "h2utils.h"

#include <string>

using namespace kuma;

H2StreamProxy::H2StreamProxy(const EventLoopPtr &loop)
: loop_(loop)
{
    loop_token_.eventLoop(loop);
    KM_SetObjKey("H2StreamProxy");
}

H2StreamProxy::~H2StreamProxy()
{
    close();
    while (!send_buf_queue_.empty()) {
        send_buf_queue_.pop_front();
    }
    while (!recv_buf_queue_.empty()) {
        recv_buf_queue_.pop_front();
    }
    conn_token_.reset();
    loop_token_.reset();
}

KMError H2StreamProxy::setProxyInfo(const ProxyInfo &proxy_info)
{
    proxy_info_ = proxy_info;
    
    return KMError::NOERR;
}

KMError H2StreamProxy::addHeader(std::string name, std::string value)
{
    if (name == H2HeaderProtocol) {
        protocol_ = value;
    }
    return outgoing_header_.addHeader(std::move(name), std::move(value));
}

KMError H2StreamProxy::sendRequest(std::string method, std::string url, uint32_t ssl_flags)
{
    KM_INFOXTRACE("sendRequest, method=" << method << ", url=" << url << ", flags=" << ssl_flags);
    auto loop = loop_.lock();
    if (!loop) {
        return KMError::INVALID_STATE;
    }
    
    if(!uri_.parse(url)) {
        return KMError::INVALID_PARAM;
    }
    is_server_ = false;
    method_ = std::move(method);
    ssl_flags_ = ssl_flags;
    
    setState(State::CONNECTING);
    
    uint16_t port = 80;
    ssl_flags = SSL_NONE;
    if (kev::is_equal("https", uri_.getScheme()) || kev::is_equal("wss", uri_.getScheme())) {
        ssl_flags = SSL_ENABLE | ssl_flags_;
        port = 443;
    }
    if(!uri_.getPort().empty()) {
        port = std::stoi(uri_.getPort());
    }
    
    auto &conn_mgr = H2ConnectionMgr::getRequestConnMgr(ssl_flags != SSL_NONE);
    conn_ = conn_mgr.getConnection(uri_.getHost(), port, ssl_flags, loop, proxy_info_);
    if (!conn_ || !conn_->eventLoop()) {
        KM_ERRXTRACE("sendRequest, failed to get H2Connection");
        return KMError::INVALID_PARAM;
    }
    auto conn_loop = conn_->eventLoop();
    conn_loop_ = conn_loop;
    conn_token_.eventLoop(conn_loop);
    is_same_loop_ = conn_->isInSameThread();
    if (is_same_loop_) {
        return sendRequest_i();
    } else if (!conn_->async([this] {
        auto err = sendRequest_i();
        if (err != KMError::NOERR) {
            onError_i(err);
        }
    }, &conn_token_)) {
        KM_ERRXTRACE("sendRequest, failed to run on H2Connection, key="<<conn_->getConnectionKey());
        return KMError::INVALID_STATE;
    }
    return KMError::NOERR;
}

KMError H2StreamProxy::attachStream(uint32_t stream_id, H2Connection::Impl* conn)
{
    if (!conn || !conn->eventLoop()) {
        return KMError::INVALID_STATE;
    }
    stream_ = conn->getStream(stream_id);
    if (!stream_) {
        return KMError::INVALID_STATE;
    }
    is_server_ = true;
    is_same_loop_ = conn->isInSameThread();
    auto conn_loop = conn->eventLoop();
    conn_loop_ = conn_loop;
    conn_token_.eventLoop(conn_loop);
    setState(State::CONNECTING);
    setupStreamCallbacks();
    
    return KMError::NOERR;
}

KMError H2StreamProxy::sendResponse(int status_code)
{
    KM_INFOXTRACE("sendResponse, status_code=" << status_code);
    status_code_ = status_code;
    if (is_same_loop_) {
        if (sendResponse_i() == KMError::NOERR) {
            setState(State::OPEN);
        }
    } else if (!runOnStreamThread([this] {
        auto err = sendResponse_i();
        if (err != KMError::NOERR) {
            onError_i(err);
        }
    }))
    {
        KM_ERRXTRACE("sendResponse, failed to run on H2Connection");
        setState(State::IN_ERROR);
        return KMError::INVALID_STATE;
    }
    
    setState(State::OPEN);
    return KMError::NOERR;
}

KMError H2StreamProxy::sendRequest_i()
{// on conn_ thread
    if (!conn_) {
        return KMError::INVALID_STATE;
    }
    if (!conn_->isReady()) {
        conn_->addConnectListener(getObjId(), [this] (KMError err) { onConnect_i(err); });
        return KMError::NOERR;
    } else {
        if (processPushPromise()) {
            // server push is available
            return KMError::NOERR;
        } else {
            stream_ = conn_->createStream();
            setupStreamCallbacks();
            return sendHeaders_i();
        }
    }
}

KMError H2StreamProxy::sendResponse_i()
{// on conn_ thread
    return sendHeaders_i();
}

size_t H2StreamProxy::buildRequestHeaders(HeaderVector &headers)
{
    outgoing_header_.processHeader();
    size_t headers_size = 0;
    headers.emplace_back(H2HeaderMethod, method_);
    headers_size += H2HeaderMethod.size() + method_.size();
    headers.emplace_back(H2HeaderScheme, uri_.getScheme());
    headers_size += H2HeaderScheme.size() + uri_.getScheme().size();
    std::string path = uri_.getPath();
    if(!uri_.getQuery().empty()) {
        path += "?";
        path += uri_.getQuery();
    }
    if(!uri_.getFragment().empty()) {
        path += "#";
        path += uri_.getFragment();
    }
    headers.emplace_back(H2HeaderPath, path);
    headers_size += H2HeaderPath.size() + path.size();
    headers.emplace_back(H2HeaderAuthority, uri_.getHost());
    headers_size += H2HeaderAuthority.size() + uri_.getHost().size();
    for (auto const &kv : outgoing_header_.getHeaders()) {
        headers.emplace_back(kv.first, kv.second);
        headers_size += kv.first.size() + kv.second.size();
    }
    return headers_size;
}

size_t H2StreamProxy::buildResponseHeaders(HeaderVector &headers)
{
    outgoing_header_.processHeader(status_code_, method_);
    size_t headers_size = 0;
    std::string str_status_code = std::to_string(status_code_);
    headers.emplace_back(H2HeaderStatus, str_status_code);
    headers_size += H2HeaderStatus.size() + str_status_code.size();
    for (auto const &kv : outgoing_header_.getHeaders()) {
        headers.emplace_back(kv.first, kv.second);
        headers_size += kv.first.size() + kv.second.size();
    }
    return headers_size;
}

KMError H2StreamProxy::sendHeaders_i()
{// on conn_ thread
    HeaderVector headers;
    size_t headers_size = 0;
    if (is_server_) {
        headers_size = buildResponseHeaders(headers);
    } else {
        headers_size = buildRequestHeaders(headers);
    }
    bool end_stream = protocol_.empty() && !outgoing_header_.isChunked() &&
        (!outgoing_header_.hasContentLength() || outgoing_header_.getContentLength() == 0);
    auto ret = stream_->sendHeaders(headers, headers_size, end_stream);
    if (ret == KMError::NOERR) {
        setState(State::OPEN);
        if (end_stream) {
            onOutgoingComplete_i();
        } else {
            onWrite_i();
        }
    }
    return ret;
}

void H2StreamProxy::setupStreamCallbacks()
{
    stream_->setHeadersCallback([this] (const HeaderVector &headers, bool endSteam) {
        onHeaders_i(headers, endSteam);
    });
    stream_->setDataCallback([this] (KMBuffer &buf, bool endSteam) {
        onData_i(buf, endSteam);
    });
    stream_->setRSTStreamCallback([this] (int err) {
        onRSTStream_i(err);
    });
    stream_->setWriteCallback([this] {
        onWrite_i();
    });
}

void H2StreamProxy::onConnect_i(KMError err)
{// on conn_ thread
    if(err != KMError::NOERR) {
        onError_i(err);
        return ;
    }
    if (kev::is_equal(method_, "CONNECT") && !protocol_.empty() &&
        !conn_->isConnectProtocolEnabled())
    {
        KM_ERRXTRACE("onConnect_i, connect protocol is not enabled, proto=" << protocol_);
        onError_i(KMError::NOT_SUPPORTED);
        return;
    }
    stream_ = conn_->createStream();
    setupStreamCallbacks();
    sendHeaders_i();
}

void H2StreamProxy::onError_i(KMError err)
{
    runOnLoopThread([this, err] { onError(err); });
}

bool H2StreamProxy::canSendData() const
{
    if (getState() != State::OPEN) {
        return false;
    }
    if (write_blocked_) {
        return false;
    }
    
    return true;
}

int H2StreamProxy::sendData(const void* data, size_t len)
{
    if (!canSendData()) {
        return 0;
    }
    if (is_same_loop_ && send_buf_queue_.empty()) {
        return sendData_i(data, len); // return the bytes sent directly
    } else {
        saveRequestData(data, len);
        if (send_buf_queue_.size() <= 1) {
            runOnStreamThread([this] { sendData_i(); });
        }
        return int(len);
    }
}

int H2StreamProxy::sendData(const KMBuffer &buf)
{
    if (!canSendData()) {
        return 0;
    }
    if (is_same_loop_ && send_buf_queue_.empty()) {
        return sendData_i(buf); // return the bytes sent directly
    } else {
        saveRequestData(buf);
        if (send_buf_queue_.size() <= 1) {
            runOnStreamThread([this] { sendData_i(); });
        }
        return int(buf.chainLength());
    }
}

int H2StreamProxy::sendData_i(const void* data, size_t len)
{// on conn_ thread
    if (getState() != State::OPEN) {
        return 0;
    }
    int ret = 0;
    bool end_stream = !data && !len;
    size_t send_len = len;
    if (outgoing_header_.hasContentLength() && body_bytes_sent_ + send_len >= outgoing_header_.getContentLength()) {
        send_len = outgoing_header_.getContentLength() - body_bytes_sent_;
        end_stream = true;
    }
    ret = stream_->sendData(data, send_len, end_stream);
    if (ret > 0) {
        body_bytes_sent_ += ret;
        if (end_stream) {
            onOutgoingComplete_i();
        }
    } else if (ret == 0) {
        write_blocked_ = true;
    }
    return ret;
}

int H2StreamProxy::sendData_i(const KMBuffer &buf)
{// on conn_ thread
    if (getState() != State::OPEN) {
        return 0;
    }
    int ret = 0;
    auto chain_len = buf.chainLength();
    bool end_stream = !chain_len;
    size_t send_len = chain_len;
    if (outgoing_header_.hasContentLength() && body_bytes_sent_ + send_len >= outgoing_header_.getContentLength()) {
        send_len = outgoing_header_.getContentLength() - body_bytes_sent_;
        end_stream = true;
    }
    ret = stream_->sendData(buf, end_stream);
    if (ret > 0) {
        body_bytes_sent_ += ret;
        if (end_stream) {
            onOutgoingComplete_i();
        }
    } else if (ret == 0) {
        write_blocked_ = true;
    }
    return ret;
}

int H2StreamProxy::sendData_i()
{// on conn_ thread
    int bytes_sent = 0;
    while (!send_buf_queue_.empty()) {
        auto &kmb = send_buf_queue_.front();
        int ret = sendData_i(*kmb);
        if (ret > 0) {
            bytes_sent += ret;
            send_buf_queue_.pop_front();
        } else if (ret == 0) {
            break;
        } else {
            onError_i(KMError::FAILED);
            return -1;
        }
    }
    return bytes_sent;
}

void H2StreamProxy::onHeaders_i(const HeaderVector &headers, bool end_stream)
{// on conn_ thread
    HeaderVector in_headers;
    if (isServer()) {
        if (!processH2RequestHeaders(headers, method_, path_, in_headers)) {
            return;
        }
    } else {
        if (!processH2ResponseHeaders(headers, status_code_, in_headers)) {
            return;
        }
    }
    incoming_header_.setHeaders(std::move(in_headers));
    header_complete_ = true;
    if (isServer()) {
        protocol_ = incoming_header_.getHeader(H2HeaderProtocol);
    }
    
    runOnLoopThread([this, end_stream]{ onHeaders(end_stream); });
}

void H2StreamProxy::onData_i(KMBuffer &buf, bool end_stream)
{// on conn_ thread
    if (is_same_loop_ && recv_buf_queue_.empty()) {
        DESTROY_DETECTOR_SETUP();
        onStreamData(buf);
        DESTROY_DETECTOR_CHECK_VOID();
        
        if (end_stream) {
            onIncomingComplete();
        }
    } else {
        saveResponseData(buf);
        runOnLoopThread([this, end_stream] { onData(end_stream); });
    }
}

void H2StreamProxy::onRSTStream_i(int err)
{// on conn_ thread
    onError_i(KMError::FAILED);
}

void H2StreamProxy::onWrite_i()
{// on conn_ thread
    if (sendData_i() < 0 || !send_buf_queue_.empty()) {
        return;
    }
    write_blocked_ = false;
    
    runOnLoopThread([this] { onWrite(); });
}

void H2StreamProxy::onOutgoingComplete_i()
{
    if (isServer()) {
        runOnLoopThread([this] { onOutgoingComplete(); }, false);
    }
}

void H2StreamProxy::onIncomingComplete_i()
{
    runOnLoopThread([this] { onIncomingComplete(); });
}

void H2StreamProxy::saveRequestData(const void *data, size_t len)
{
    KMBuffer buf(data, len, len);
    KMBuffer::Ptr kmb(buf.clone());
    send_buf_queue_.enqueue(std::move(kmb));
}

void H2StreamProxy::saveRequestData(const KMBuffer &buf)
{
    KMBuffer::Ptr kmb(buf.clone());
    send_buf_queue_.enqueue(std::move(kmb));
}

void H2StreamProxy::saveResponseData(const void *data, size_t len)
{
    KMBuffer buf(data, len, len);
    KMBuffer::Ptr kmb(buf.clone());
    recv_buf_queue_.enqueue(std::move(kmb));
}

void H2StreamProxy::saveResponseData(const KMBuffer &buf)
{
    KMBuffer::Ptr kmb(buf.clone());
    recv_buf_queue_.enqueue(std::move(kmb));
}

void H2StreamProxy::onHeaders(bool end_stream)
{// on loop_ thread
    if (header_complete_) {
        DESTROY_DETECTOR_SETUP();
        onHeaderComplete();
        DESTROY_DETECTOR_CHECK_VOID();
    }
    if (end_stream) {
        onIncomingComplete();
    }
}

void H2StreamProxy::onData(bool end_stream)
{// on loop_ thread
    while (!recv_buf_queue_.empty()) {
        auto &kmb = recv_buf_queue_.front();
        DESTROY_DETECTOR_SETUP();
        if (kmb) onStreamData(*kmb);
        DESTROY_DETECTOR_CHECK_VOID();
        recv_buf_queue_.pop_front();
    }
    if (end_stream) {
        onIncomingComplete();
    }
}

void H2StreamProxy::onPushPromise(bool end_stream)
{// on loop_ thread
    checkResponseStatus(end_stream);
}

void H2StreamProxy::checkResponseStatus(bool end_stream)
{// on loop_ thread
    if (getState() != State::OPEN) {
        return;
    }
    if (header_complete_) {
        bool end = end_stream && recv_buf_queue_.empty();
        onHeaders(end);
        if (end) {
            return;
        }
    }
    onData(end_stream);
}

void H2StreamProxy::onHeaderComplete()
{
    if (header_cb_) header_cb_();
}

void H2StreamProxy::onStreamData(KMBuffer &buf)
{
    if (data_cb_) data_cb_(buf);
}

void H2StreamProxy::onOutgoingComplete()
{
    if (outgoing_complete_cb_) outgoing_complete_cb_();
}

void H2StreamProxy::onIncomingComplete()
{
    if (incoming_complete_cb_) incoming_complete_cb_();
}

void H2StreamProxy::onWrite()
{// on loop_ thread
    if (write_cb_) write_cb_(KMError::NOERR);
}

void H2StreamProxy::onError(KMError err)
{// on loop_ thread
    if(error_cb_) error_cb_(err);
}

void H2StreamProxy::reset()
{
    while (!send_buf_queue_.empty()) {
        send_buf_queue_.pop_front();
    }
    while (!recv_buf_queue_.empty()) {
        recv_buf_queue_.pop_front();
    }
    
    outgoing_header_.reset();
    incoming_header_.reset();
    stream_->close();
    stream_.reset();
    
    is_same_loop_ = false;
    body_bytes_sent_ = 0;
    ssl_flags_ = 0;
    status_code_ = 0;
    header_complete_ = false;
    write_blocked_ = false;
}

KMError H2StreamProxy::close()
{
    if (conn_) {
        conn_->sync([this] { close_i(); });
    } else {
        close_i();
    }
    conn_token_.reset();
    loop_token_.reset();
    conn_.reset();
    return KMError::NOERR;
}

void H2StreamProxy::close_i()
{// on conn_ thread
    if (getState() == State::CONNECTING && conn_) {
        conn_->removeConnectListener(getObjId());
    }
    if (stream_) {
        stream_->close();
        stream_.reset();
    }
    setState(State::CLOSED);
}

bool H2StreamProxy::processPushPromise()
{// on conn_ thread
    if (!kev::is_equal(method_, "GET")) {
        return false;
    }
    if (outgoing_header_.hasBody()) {
        return false;
    }
    std::string cache_key = getCacheKey();
    auto push_client = conn_->getPushClient(cache_key);
    if (!push_client) {
        return false;
    }
    
    setState(State::OPEN);
    
    HeaderVector h2_headers;
    HeaderVector rsp_headers;
    push_client->getResponseHeaders(h2_headers);
    KMBuffer rsp_body;
    push_client->getResponseBody(rsp_body);
    header_complete_ = push_client->isHeaderComplete();
    bool end_stream = push_client->isComplete();
    if (header_complete_) {
        if (!processH2ResponseHeaders(h2_headers, status_code_, rsp_headers)) {
            return false;
        }
        incoming_header_.setHeaders(std::move(rsp_headers));
    }
    if (!rsp_body.empty()) {
        saveResponseData(rsp_body);
    }
    auto stream = push_client->release();
    if (stream) {
        if (!end_stream) {
            stream_ = std::move(stream);
            setupStreamCallbacks();
        } else {
            stream->close();
        }
    }
    
    runOnLoopThread([this, end_stream] { onPushPromise(end_stream); }, false);
    return true;
}
