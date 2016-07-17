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
#include "HPacker.h"
#include "H2Stream.h"
#include "TcpSocketImpl.h"
#include "http/HttpParserImpl.h"

#include <map>

using namespace hpack;

KUMA_NS_BEGIN

class H2ConnectionImpl : public KMObject, public FrameCallback
{
public:
    typedef std::function<void(int)> ConnectCallback;
    
    H2ConnectionImpl(EventLoopImpl* loop);
	~H2ConnectionImpl();
    
    int setSslFlags(uint32_t ssl_flags);
    int connect(const std::string &host, uint16_t port, ConnectCallback cb);
    int attachFd(SOCKET_FD fd, const uint8_t* data=nullptr, size_t size=0);
    int attachSocket(TcpSocketImpl&& tcp, HttpParserImpl&& parser);
    int close();
    
    int sendH2Frame(H2Frame *frame);
    
    int sendWindowUpdate(uint32_t increment);
    int sendHeadersFrame(HeadersFrame *frame);
    
    bool isReady() { return getState() == State::OPEN; }
    
    void setConnectionKey(const std::string &key) { key_ = key; }
    
    H2StreamPtr createStream();
    void removeStream(uint32_t streamId);
    
public:
    void onFrame(H2Frame *frame);
    void onFrameError(const FrameHeader &hdr, H2Error err);
    
private:
    void onConnect(int err);
    void onSend(int err);
    void onReceive(int err);
    void onClose(int err);
    
private:
    void onHttpData(const char* data, size_t len);
    void onHttpEvent(HttpEvent ev);
    
private:
    int connect_i(const std::string &host, uint16_t port);
    int handleInputData(const uint8_t *buf, size_t len);
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
    H2StreamPtr getStream(uint32_t streamId);
    
    void remove();
    
    std::string buildUpgradeRequest();
    std::string buildUpgradeResponse();
    void sendUpgradeRequest();
    void sendUpgradeResponse();
    void sendPreface();
    int handleUpgradeRequest();
    int handleUpgradeResponse();
    
    enum State {
        IDLE,
        CONNECTING,
        HANDSHAKE,
        SENDING_PREFACE,
        OPEN,
        IN_ERROR,
        CLOSED
    };
    void setState(State state) { state_ = state; }
    State getState() { return state_; }
    void onStateOpen();
    void cleanup();
    
private:
    EventLoopImpl* loop_;
    TcpSocketImpl tcp_;
    
    State state_ = State::IDLE;
    ConnectCallback cb_connect_;
    
    std::string key_;
    std::string host_;
    uint16_t port_ = 0;
    
    uint8_t* initData_ = nullptr;
    size_t initSize_ = 0;
    
    HttpParserImpl httpParser_;
    FrameParser frameParser_;
    HPacker hpEncoder_;
    HPacker hpDecoder_;
    
    std::map<uint32_t, H2StreamPtr> streams_;
    std::map<uint32_t, H2StreamPtr> pushStreams_;
    
    bool isServer_ = false;
    bool sendSettingAck = false;
    std::vector<uint8_t>    send_buffer_;
    size_t                  send_offset_ = 0;
    
    uint32_t nextStreamId_ = 0;
    
    bool* destroy_flag_ptr_ = nullptr;
};

using H2ConnectionPtr = std::shared_ptr<H2ConnectionImpl>;

KUMA_NS_END

#endif
