/* Copyright (c) 2016, Fengping Bao <jamol@live.com>
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

#include "Http2Request.h"
#include "http/Uri.h"
#include "http/HttpCache.h"
#include "H2ConnectionMgr.h"
#include "util/kmtrace.h"
#include "util/util.h"
#include "h2utils.h"

#include <sstream>
#include <algorithm>
#include <string>

using namespace kuma;

Http2Request::Http2Request(const EventLoopPtr &loop, std::string ver)
: HttpRequest::Impl(std::move(ver)), loop_(loop)
{
    loop_token_.eventLoop(loop);
    KM_SetObjKey("Http2Request");
}

Http2Request::~Http2Request()
{
    while (!req_queue_.empty()) {
        req_queue_.pop_front();
    }
    while (!rsp_queue_.empty()) {
        rsp_queue_.pop_front();
    }
    conn_token_.reset();
    loop_token_.reset();
}

KMError Http2Request::setSslFlags(uint32_t ssl_flags)
{
    ssl_flags_ = ssl_flags;
    return KMError::NOERR;
}

void Http2Request::addHeader(std::string name, std::string value)
{
    transform(name.begin(), name.end(), name.begin(), ::tolower);
    if(!name.empty()) {
        if (is_equal("Transfer-Encoding", name) && is_equal("chunked", value)) {
            is_chunked_ = true;
            return; // omit chunked
        }
        HttpHeader::addHeader(std::move(name), std::move(value));
    }
}

KMError Http2Request::sendRequest()
{
    std::string str_port = uri_.getPort();
    uint16_t port = 80;
    uint32_t ssl_flags = SSL_NONE;
    if (is_equal("https", uri_.getScheme())) {
        ssl_flags = SSL_ENABLE | ssl_flags_;
        port = 443;
    }
    if(!str_port.empty()) {
        port = std::stoi(str_port);
    }
    
    setState(State::CONNECTING);
    auto loop = loop_.lock();
    if (!loop) {
        return KMError::INVALID_STATE;
    }
    
    if (processHttpCache(loop)) {
        // cache hit
        return KMError::NOERR;
    }
    
    auto &conn_mgr = H2ConnectionMgr::getRequestConnMgr(ssl_flags != SSL_NONE);
    conn_ = conn_mgr.getConnection(uri_.getHost(), port, ssl_flags, loop);
    if (!conn_ || !conn_->eventLoop()) {
        KUMA_ERRXTRACE("sendRequest, failed to get H2Connection");
        return KMError::INVALID_PARAM;
    } else {
        conn_token_.eventLoop(conn_->eventLoop());
        if (conn_->isInSameThread()) {
            return sendRequest_i();
        } else if (!conn_->async([this] {
            auto err = sendRequest_i();
            if (err != KMError::NOERR) {
                onError(err);
            }
        }, &conn_token_)) {
            KUMA_ERRXTRACE("sendRequest, failed to run in H2Connection, key="<<conn_->getConnectionKey());
            return KMError::INVALID_STATE;
        }
    }
    return KMError::NOERR;
}

KMError Http2Request::sendRequest_i()
{// on conn_ thread
    if (!conn_) {
        return KMError::INVALID_STATE;
    }
    if (!conn_->isReady()) {
        conn_->addConnectListener(getObjId(), [this] (KMError err) { onConnect(err); });
        return KMError::NOERR;
    } else {
        if (processPushPromise()) {
            // server push is available
            return KMError::NOERR;
        } else {
            return sendHeaders();
        }
    }
}

bool Http2Request::processHttpCache(const EventLoopPtr &loop)
{
    if (!HttpCache::isCacheable(method_, header_vec_)) {
        return false;
    }
    std::string cache_key = getCacheKey();
    
    int status_code = 0;
    HeaderVector rsp_headers;
    KMBuffer rsp_body;
    if (HttpCache::instance().getCache(cache_key, status_code, rsp_headers, rsp_body)) {
        // cache hit
        setState(State::RECVING_RESPONSE);
        status_code_ = status_code;
        rsp_headers_.swap(rsp_headers);
        if (!rsp_body.empty()) {
            saveResponseData(rsp_body);
        }
        header_complete_ = true;
        response_complete_ = true;
        loop->post([this] {
            onCacheComplete();
        }, &loop_token_);
        return true;
    }
    return false;
}

bool Http2Request::processPushPromise()
{// on conn_ thread
    if (!is_equal(method_, "GET")) {
        return false;
    }
    if (HttpHeader::hasBody()) {
        return false;
    }
    std::string cache_key = getCacheKey();
    auto push_client = conn_->getPushClient(cache_key);
    if (!push_client) {
        return false;
    }

    setState(State::RECVING_RESPONSE);
    
    HeaderVector h2_headers;
    push_client->getResponseHeaders(h2_headers);
    KMBuffer rsp_body;
    push_client->getResponseBody(rsp_body);
    header_complete_ = push_client->isHeaderComplete();
    response_complete_ = push_client->isComplete();
    if (header_complete_ && !processH2ResponseHeaders(h2_headers, status_code_, rsp_headers_)) {
        return false;
    }
    if (!rsp_body.empty()) {
        saveResponseData(rsp_body);
    }
    auto stream = push_client->release();
    if (stream) {
        if (!response_complete_) {
            stream_ = std::move(stream);
            setupStreamCallbacks();
        } else {
            stream->close();
        }
    }
    auto loop = loop_.lock();
    loop->post([this] { onPushPromise(); });
    return true;
}

const std::string& Http2Request::getHeaderValue(std::string name) const
{
    for (auto const &kv : rsp_headers_) {
        if (is_equal(kv.first, name)) {
            return kv.second;
        }
    }
    return EmptyString;
}

void Http2Request::forEachHeader(EnumrateCallback cb)
{
    for (auto &kv : rsp_headers_) {
        cb(kv.first, kv.second);
    }
}

void Http2Request::checkHeaders()
{
    if(!hasHeader("accept")) {
        addHeader("accept", "*/*");
    }
    if(!hasHeader("content-type")) {
        addHeader("content-type", "application/octet-stream");
    }
    if(!hasHeader("user-agent")) {
        addHeader("user-agent", UserAgent);
    }
    if(!hasHeader("cache-control")) {
        addHeader("cache-control", "no-cache");
    }
    if(!hasHeader("pragma")) {
        addHeader("pragma", "no-cache");
    }
}

size_t Http2Request::buildHeaders(HeaderVector &headers)
{
    HttpHeader::processHeader();
    size_t headers_size = 0;
    headers.emplace_back(std::make_pair(H2HeaderMethod, method_));
    headers_size += H2HeaderMethod.size() + method_.size();
    headers.emplace_back(std::make_pair(H2HeaderScheme, uri_.getScheme()));
    headers_size += H2HeaderScheme.size() + uri_.getScheme().size();
    std::string path = uri_.getPath();
    if(!uri_.getQuery().empty()) {
        path = "?" + uri_.getQuery();
    }
    if(!uri_.getFragment().empty()) {
        path = "#" + uri_.getFragment();
    }
    headers.emplace_back(std::make_pair(H2HeaderPath, path));
    headers_size += H2HeaderPath.size() + path.size();
    headers.emplace_back(std::make_pair(H2HeaderAuthority, uri_.getHost()));
    headers_size += H2HeaderAuthority.size() + uri_.getHost().size();
    for (auto const &kv : header_vec_) {
        headers.emplace_back(kv.first, kv.second);
        headers_size += kv.first.size() + kv.second.size();
    }
    return headers_size;
}

KMError Http2Request::sendHeaders()
{// on conn_ thread
    stream_ = conn_->createStream();
    setupStreamCallbacks();
    setState(State::SENDING_HEADER);
    
    HeaderVector headers;
    size_t headers_size = buildHeaders(headers);
    bool end_stream = !has_content_length_ && !is_chunked_;
    auto ret = stream_->sendHeaders(headers, headers_size, end_stream);
    if (ret == KMError::NOERR) {
        if (end_stream) {
            setState(State::RECVING_RESPONSE);
        } else {
            setState(State::SENDING_BODY);
            auto loop = conn_->eventLoop();
            if (loop) {
                loop->post([this] { onWrite(); }, &conn_token_);
            }
        }
    }
    return ret;
}

void Http2Request::setupStreamCallbacks()
{
    stream_->setHeadersCallback([this] (const HeaderVector &headers, bool endSteam) {
        onHeaders(headers, endSteam);
    });
    stream_->setDataCallback([this] (KMBuffer &buf, bool endSteam) {
        onData(buf, endSteam);
    });
    stream_->setRSTStreamCallback([this] (int err) {
        onRSTStream(err);
    });
    stream_->setWriteCallback([this] {
        onWrite();
    });
}

void Http2Request::onConnect(KMError err)
{// on conn_ thread
    if(err != KMError::NOERR) {
        onError(err);
        return ;
    }
    sendHeaders();
}

void Http2Request::onError(KMError err)
{
    auto loop = loop_.lock();
    if (!loop || loop->inSameThread()) {
        onError_i(err);
    } else {
        loop->post([this, err] { onError_i(err); }, &loop_token_);
    }
}

int Http2Request::sendData(const void* data, size_t len)
{
    if (!conn_) {
        return -1;
    }
    if (getState() != State::SENDING_BODY) {
        return 0;
    }
    if (write_blocked_) {
        return 0;
    }
    if (conn_->isInSameThread() && req_queue_.empty()) {
        return sendData_i(data, len); // return the bytes sent directly
    } else {
        saveRequestData(data, len);
        if (req_queue_.size() <= 1) {
            conn_->async([this] { sendData_i(); }, &conn_token_);
        }
        return int(len);
    }
}

int Http2Request::sendData(const KMBuffer &buf)
{
    if (!conn_) {
        return -1;
    }
    if (getState() != State::SENDING_BODY) {
        return 0;
    }
    if (write_blocked_) {
        return 0;
    }
    if (conn_->isInSameThread() && req_queue_.empty()) {
        return sendData_i(buf); // return the bytes sent directly
    } else {
        saveRequestData(buf);
        if (req_queue_.size() <= 1) {
            conn_->async([this] { sendData_i(); }, &conn_token_);
        }
        return int(buf.chainLength());
    }
}

int Http2Request::sendData_i(const void* data, size_t len)
{// on conn_ thread
    if (getState() != State::SENDING_BODY) {
        return 0;
    }
    int ret = 0;
    size_t send_len = len;
    if (data && len) {
        if (has_content_length_ && body_bytes_sent_ + send_len > content_length_) {
            send_len = content_length_ - body_bytes_sent_;
        }
        ret = stream_->sendData(data, send_len, false);
        if (ret > 0) {
            body_bytes_sent_ += ret;
        }
    }
    bool end_stream = (!data && !len) || (has_content_length_ && body_bytes_sent_ >= content_length_);
    if (end_stream) {
        stream_->sendData(nullptr, 0, true);
        setState(State::RECVING_RESPONSE);
    }
    if (ret == 0) {
        write_blocked_ = true;
    }
    return ret;
}

int Http2Request::sendData_i(const KMBuffer &buf)
{// on conn_ thread
    if (getState() != State::SENDING_BODY) {
        return 0;
    }
    int ret = 0;
    auto chain_len = buf.chainLength();
    size_t send_len = chain_len;
    if (send_len) {
        if (has_content_length_ && body_bytes_sent_ + send_len > content_length_) {
            send_len = content_length_ - body_bytes_sent_;
        }
        ret = stream_->sendData(buf, false);
        if (ret > 0) {
            body_bytes_sent_ += ret;
        }
    }
    bool end_stream = (!chain_len) || (has_content_length_ && body_bytes_sent_ >= content_length_);
    if (end_stream) {
        stream_->sendData(nullptr, 0, true);
        setState(State::RECVING_RESPONSE);
    }
    if (ret == 0) {
        write_blocked_ = true;
    }
    return ret;
}

int Http2Request::sendData_i()
{// on conn_ thread
    int bytes_sent = 0;
    while (!req_queue_.empty()) {
        auto &kmb = req_queue_.front();
        int ret = sendData_i(*kmb);
        if (ret > 0) {
            bytes_sent += ret;
            req_queue_.pop_front();
        } else if (ret == 0) {
            break;
        } else {
            onError(KMError::FAILED);
            return -1;
        }
    }
    return bytes_sent;
}

void Http2Request::onHeaders(const HeaderVector &headers, bool end_stream)
{// on conn_ thread
    if (!processH2ResponseHeaders(headers, status_code_, rsp_headers_)) {
        return;
    }
    header_complete_ = true;
    response_complete_ = end_stream;
    auto loop = loop_.lock();
    if (!loop || loop->inSameThread()) {
        onHeaders();
    } else {
        loop->post([this]{ onHeaders(); }, &loop_token_);
    }
}

void Http2Request::onData(KMBuffer &buf, bool end_stream)
{// on conn_ thread
    response_complete_ = end_stream;
    auto loop = loop_.lock();
    if (!loop || (loop->inSameThread() && rsp_queue_.empty())) {
        DESTROY_DETECTOR_SETUP();
        if (data_cb_ && buf.chainLength() > 0) data_cb_(buf);
        DESTROY_DETECTOR_CHECK_VOID();
        
        if (end_stream) {
            onComplete();
        }
    } else {
        saveResponseData(buf);
        if (rsp_queue_.size() <= 1) {
            loop->post([this] { onData(); }, &loop_token_);
        }
    }
}

void Http2Request::onRSTStream(int err)
{// on conn_ thread
    onError(KMError::FAILED);
}

void Http2Request::onWrite()
{// on conn_ thread
    if (sendData_i() < 0 || !req_queue_.empty()) {
        return;
    }
    write_blocked_ = false;
    
    auto loop = loop_.lock();
    if (!loop || loop->inSameThread()) {
        onWrite_i();
    } else {
        loop->post([this] { onWrite_i(); }, &loop_token_);
    }
}

void Http2Request::saveRequestData(const void *data, size_t len)
{
    KMBuffer buf(data, len, len);
    KMBuffer::Ptr kmb(buf.clone());
    req_queue_.enqueue(std::move(kmb));
}

void Http2Request::saveRequestData(const KMBuffer &buf)
{
    KMBuffer::Ptr kmb(buf.clone());
    req_queue_.enqueue(std::move(kmb));
}

void Http2Request::saveResponseData(const void *data, size_t len)
{
    KMBuffer buf(data, len, len);
    KMBuffer::Ptr kmb(buf.clone());
    rsp_queue_.enqueue(std::move(kmb));
}

void Http2Request::saveResponseData(const KMBuffer &buf)
{
    KMBuffer::Ptr kmb(buf.clone());
    rsp_queue_.enqueue(std::move(kmb));
}

void Http2Request::onHeaders()
{// on loop_ thread
    if (getState() != State::RECVING_RESPONSE) {
        return;
    }
    if (header_complete_ && header_cb_) {
        DESTROY_DETECTOR_SETUP();
        header_cb_();
        DESTROY_DETECTOR_CHECK_VOID();
    }
    if (response_complete_) {
        onComplete();
    }
}

void Http2Request::onData()
{// on loop_ thread
    if (getState() != State::RECVING_RESPONSE) {
        return;
    }
    
    while (!rsp_queue_.empty()) {
        auto &kmb = rsp_queue_.front();
        DESTROY_DETECTOR_SETUP();
        if (kmb) data_cb_(*kmb);
        DESTROY_DETECTOR_CHECK_VOID();
        rsp_queue_.pop_front();
    }
    if (response_complete_) {
        onComplete();
    }
}

void Http2Request::onComplete()
{// on loop_ thread
    setState(State::COMPLETE);
    if (response_cb_) response_cb_();
}

void Http2Request::onCacheComplete()
{// on loop_ thread
    checkResponseStatus();
}

void Http2Request::onPushPromise()
{// on loop_ thread
    checkResponseStatus();
}

void Http2Request::checkResponseStatus()
{// on loop_ thread
    if (getState() != State::RECVING_RESPONSE) {
        return;
    }
    if (header_complete_ && header_cb_) {
        DESTROY_DETECTOR_SETUP();
        header_cb_();
        DESTROY_DETECTOR_CHECK_VOID();
    }
    onData();
}

void Http2Request::onWrite_i()
{// on loop_ thread
    if(write_cb_) write_cb_(KMError::NOERR);
}

void Http2Request::onError_i(KMError err)
{// on loop_ thread
    if(error_cb_) error_cb_(err);
}

void Http2Request::reset()
{
    HttpRequest::Impl::reset();
    
    while (!req_queue_.empty()) {
        req_queue_.pop_front();
    }
    while (!rsp_queue_.empty()) {
        rsp_queue_.pop_front();
    }
    
    rsp_headers_.clear();
    stream_->close();
    stream_.reset();
    
    body_bytes_sent_ = 0;
    ssl_flags_ = 0;
    status_code_ = 0;
    header_complete_ = false;
    response_complete_ = false;
    closing_ = false;
    write_blocked_ = false;
    
    if (getState() == State::COMPLETE) {
        setState(State::WAIT_FOR_REUSE);
    }
}

KMError Http2Request::close()
{
    closing_ = true;
    if (conn_) {
        conn_->sync([this] { close_i(); });
    }
    conn_.reset();
    conn_token_.reset();
    loop_token_.reset();
    return KMError::NOERR;
}

void Http2Request::close_i()
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
