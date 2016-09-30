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
#include "TcpSocketImpl.h"
#include "TcpConnection.h"
#include "http/HttpParserImpl.h"
#include "EventLoopImpl.h"
#include "util/DestroyDetector.h"

#include <map>

using namespace hpack;

KUMA_NS_BEGIN

const uint32_t LOCAL_CONN_INITIAL_WINDOW_SIZE = 20*1024*1024;
const uint32_t LOCAL_STREAM_INITIAL_WINDOW_SIZE = 6*1024*1024;

class Http2Response;

class H2Connection::Impl : public KMObject, public DestroyDetector, public FrameCallback, public TcpConnection, public EventLoop::Impl::Listener
{
public:
    using ConnectCallback = std::function<void(KMError)>;
    using AcceptCallback = H2Connection::AcceptCallback;
    using ErrorCallback = H2Connection::ErrorCallback;
    
    Impl(EventLoop::Impl* loop);
	~Impl();
    
    KMError connect(const std::string &host, uint16_t port, ConnectCallback cb);
    KMError attachFd(SOCKET_FD fd, const uint8_t* data=nullptr, size_t size=0);
    KMError attachSocket(TcpSocket::Impl&& tcp, HttpParser::Impl&& parser);
    KMError attachStream(uint32_t streamId, HttpResponse::Impl* rsp);
    KMError close();
    void setAcceptCallback(AcceptCallback cb) { accept_cb_ = std::move(cb); }
    void setErrorCallback(ErrorCallback cb) { error_cb_ = std::move(cb); }
    
    KMError sendH2Frame(H2Frame *frame);
    
    bool isReady() { return getState() == State::OPEN; }
    
    void setConnectionKey(const std::string &key);
    
    H2StreamPtr createStream();
    H2StreamPtr createStream(uint32_t streamId);
    H2StreamPtr getStream(uint32_t streamId);
    void removeStream(uint32_t streamId);
    
    uint32_t remoteWindowSize() { return flow_ctrl_.remoteWindowSize(); }
    void appendBlockedStream(uint32_t streamId);
    
    void loopStopped() override;
    
public:
    void onFrame(H2Frame *frame) override;
    void onFrameError(const FrameHeader &hdr, H2Error err, bool stream) override;
    
private:
    void onConnect(KMError err) override;
    KMError handleInputData(uint8_t *src, size_t len) override;
    void onWrite() override;
    void onError(KMError err) override;
    
private:
    void onHttpData(const char* data, size_t len);
    void onHttpEvent(HttpEvent ev);
    
private:
    KMError connect_i(const std::string &host, uint16_t port);
    KMError sendHeadersFrame(HeadersFrame *frame);
    KMError parseInputData(const uint8_t *buf, size_t len);
    void handleDataFrame(DataFrame *frame);
    void handleHeadersFrame(HeadersFrame *frame);
    void handlePriorityFrame(PriorityFrame *frame);
    void handleRSTStreamFrame(RSTStreamFrame *frame);
    void handleSettingsFrame(SettingsFrame *frame);
    void handlePushFrame(PushPromiseFrame *frame);
    void handlePingFrame(PingFrame *frame);
    void handleGoawayFrame(GoawayFrame *frame);
    void handleWindowUpdateFrame(WindowUpdateFrame *frame);
    void handleContinuationFrame(ContinuationFrame *frame);
    
    void addStream(H2StreamPtr stream);
    
    std::string buildUpgradeRequest();
    std::string buildUpgradeResponse();
    void sendUpgradeRequest();
    void sendUpgradeResponse();
    void sendPreface();
    KMError handleUpgradeRequest();
    KMError handleUpgradeResponse();
    
    enum State {
        IDLE,
        CONNECTING,
        UPGRADING,
        HANDSHAKE,
        OPEN,
        IN_ERROR,
        CLOSED
    };
    void setState(State state) { state_ = state; }
    State getState() { return state_; }
    void onStateOpen();
    void cleanup();
    
    void onConnectError(KMError err);
    void notifyBlockedStreams();
    void sendWindowUpdate(uint32_t streamId, uint32_t delta);
    bool isControlFrame(H2Frame *frame);
    
    void applySettings(const ParamVector &params);
    void updateInitialWindowSize(uint32_t ws);
    void sendGoaway(H2Error err);
    void connectionError(H2Error err);
    void streamError(uint32_t streamId, H2Error err);
    
private:
    State state_ = State::IDLE;
    ConnectCallback connect_cb_; // client only
    AcceptCallback accept_cb_; // server only
    ErrorCallback error_cb_;
    
    std::string key_;
    
    HttpParser::Impl httpParser_;
    FrameParser frameParser_;
    HPacker hpEncoder_;
    HPacker hpDecoder_;
    
    std::map<uint32_t, H2StreamPtr> streams_;
    std::map<uint32_t, H2StreamPtr> promisedStreams_;
    std::map<uint32_t, uint32_t>    blocked_streams_;
    
    std::string             cmpPreface_; // server only
    
    uint32_t remoteFrameSize_ = H2_DEFAULT_FRAME_SIZE;
    uint32_t initRemoteWindowSize_ = H2_DEFAULT_WINDOW_SIZE;
    uint32_t initLocalWindowSize_ = LOCAL_STREAM_INITIAL_WINDOW_SIZE; // initial local stream window size
    
    FlowControl flow_ctrl_;
    
    uint32_t nextStreamId_ = 0;
    uint32_t lastStreamId_ = 0;
    
    bool prefaceReceived_ = false;
    bool registeredToLoop_ = false;
};

using H2ConnectionPtr = std::shared_ptr<H2Connection::Impl>;
using H2ConnectionWeakPtr = std::weak_ptr<H2Connection::Impl>;

KUMA_NS_END

#endif
