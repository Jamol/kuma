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

#ifndef __H2ConnectionImpl_H__
#define __H2ConnectionImpl_H__

#include "kmdefs.h"
#include "FrameParser.h"
#include "hpack/HPacker.h"
#include "H2Stream.h"
#include "PushClient.h"
#include "TcpSocketImpl.h"
#include "TcpConnection.h"
#include "http/HttpParserImpl.h"
#include "EventLoopImpl.h"
#include "util/DestroyDetector.h"

#include <map>
#include <vector>

using namespace hpack;

KUMA_NS_BEGIN

class H2Handshake;

class H2Connection::Impl : public KMObject, public DestroyDetector, public FrameCallback
{
public:
    using ConnectCallback = std::function<void(KMError)>;
    using AcceptCallback = H2Connection::AcceptCallback;
    using ErrorCallback = H2Connection::ErrorCallback;
    
    Impl(const EventLoopPtr &loop);
	~Impl();
    
    KMError setSslFlags(uint32_t ssl_flags) { return tcp_conn_.setSslFlags(ssl_flags); }
    uint32_t getSslFlags() const { return tcp_conn_.getSslFlags(); }
    bool sslEnabled() const { return tcp_conn_.sslEnabled(); }
    KMError connect(const std::string &host, uint16_t port);
    KMError attachFd(SOCKET_FD fd, const KMBuffer *init_buf);
    KMError attachSocket(TcpSocket::Impl&& tcp, HttpParser::Impl&& parser, const KMBuffer *init_buf);
    PushClient* getPushClient(const std::string &cache_key);
    KMError close();
    void setAcceptCallback(AcceptCallback cb) { accept_cb_ = std::move(cb); }
    void setErrorCallback(ErrorCallback cb) { error_cb_ = std::move(cb); }
    void addConnectListener(long uid, ConnectCallback cb);
    void removeConnectListener(long uid);
    
    KMError sendH2Frame(H2Frame *frame);
    
    bool isReady() const { return getState() == State::OPEN; }
    bool isConnectProtocolEnabled() const { return enable_connect_protocol_; }
    
    void setConnectionKey(const std::string &key);
    std::string getConnectionKey() const { return key_; }
    
    H2StreamPtr createStream();
    H2StreamPtr createStream(uint32_t stream_id);
    H2StreamPtr getStream(uint32_t stream_id);
    void removeStream(uint32_t stream_id);
    void removePushClient(uint32_t push_id);
    
    uint32_t remoteWindowSize() { return flow_ctrl_.remoteWindowSize(); }
    void appendBlockedStream(uint32_t stream_id);
    
    void onLoopActivity(LoopActivity acti);
    
    bool sync(EventLoop::Task task);
    bool async(EventLoop::Task task, EventLoopToken *token=nullptr);
    bool isInSameThread() const { return std::this_thread::get_id() == thread_id_; }
    EventLoopPtr eventLoop() { return tcp_conn_.eventLoop(); }
    
    void connectionError(H2Error err);
    void streamError(uint32_t stream_id, H2Error err);
    void streamOpened(uint32_t stream_id);
    void streamClosed(uint32_t stream_id);
    
public:
    bool onFrame(H2Frame *frame) override;
    void onFrameError(const FrameHeader &hdr, H2Error err, bool stream_err) override;
    
private:
    void onConnect(KMError err, const std::string &host);
    KMError handleInputData(uint8_t *src, size_t len);
    void onWrite();
    void onError(KMError err);
    
private:
    KMError connect_i(const std::string &host, uint16_t port);
    KMError sendData(const KMBuffer &buf);
    KMError sendHeadersFrame(HeadersFrame *frame);
    KMError parseInputData(const uint8_t *buf, size_t len);
    bool handleDataFrame(DataFrame *frame);
    bool handleHeadersFrame(HeadersFrame *frame);
    bool handlePriorityFrame(PriorityFrame *frame);
    bool handleRSTStreamFrame(RSTStreamFrame *frame);
    bool handleSettingsFrame(SettingsFrame *frame);
    bool handlePushFrame(PushPromiseFrame *frame);
    bool handlePingFrame(PingFrame *frame);
    bool handleGoawayFrame(GoawayFrame *frame);
    bool handleWindowUpdateFrame(WindowUpdateFrame *frame);
    bool handleContinuationFrame(ContinuationFrame *frame);
    
    bool handleHeadersComplete(uint32_t stream_id, const HeaderVector &header_vec);
    
    void addStream(H2StreamPtr stream);
    void addPushClient(uint32_t push_id, PushClientPtr client);
    
    void setupH2Handshake();
    void onHandshakeComplete(SettingsFrame *frame);
    void onHandshakeError(KMError err);
    
    enum State {
        IDLE,
        CONNECTING,
        HANDSHAKE,
        OPEN,
        IN_ERROR,
        CLOSED
    };
    void setState(State state) { state_ = state; }
    State getState() const { return state_; }
    void onStateOpen();
    void cleanup();
    void cleanupAndRemove();
    
    void onConnectError(KMError err);
    void notifyBlockedStreams();
    KMError sendWindowUpdate(uint32_t stream_id, uint32_t delta);
    bool isControlFrame(H2Frame *frame);
    
    bool applySettings(const ParamVector &params);
    void updateInitialWindowSize(uint32_t ws);
    void sendGoaway(H2Error err);
    
    void notifyListeners(KMError err);
    void removeSelf();
    
protected:
    State state_ = State::IDLE;
    AcceptCallback accept_cb_; // server only
    ErrorCallback error_cb_;
    
    std::thread::id thread_id_;
    
    std::map<long, ConnectCallback> connect_listeners_;
    
    std::string key_;
    
    std::unique_ptr<H2Handshake> handshake_;
    FrameParser frame_parser_;
    HPacker hp_encoder_;
    HPacker hp_decoder_;
    
    std::vector<uint8_t> headers_block_buf_;
    
    std::map<uint32_t, H2StreamPtr> streams_;
    std::map<uint32_t, H2StreamPtr> promised_streams_;
    std::map<uint32_t, uint32_t> blocked_streams_;
    
    std::map<uint32_t, PushClientPtr> push_clients_;
    
    uint32_t max_local_frame_size_ = 65536;
    uint32_t max_remote_frame_size_ = H2_DEFAULT_FRAME_SIZE;
    uint32_t init_remote_window_size_ = H2_DEFAULT_WINDOW_SIZE;
    uint32_t init_local_window_size_ = H2_LOCAL_STREAM_INITIAL_WINDOW_SIZE; // initial local stream window size
    
    FlowControl flow_ctrl_;
    
    uint32_t next_stream_id_ = 0;
    uint32_t last_stream_id_ = 0;
    
    uint32_t max_concurrent_streams_ = 128;
    uint32_t opened_stream_count_ = 0;
    
    bool enable_connect_protocol_ = false;
    bool expect_continuation_frame_ = false;
    uint32_t stream_id_of_expected_continuation_ = 0;
    
    TcpConnection tcp_conn_;
    EventLoopToken loop_token_;
};

using H2ConnectionPtr = std::shared_ptr<H2Connection::Impl>;
using H2ConnectionWeakPtr = std::weak_ptr<H2Connection::Impl>;

KUMA_NS_END

#endif
