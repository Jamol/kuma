#include "ProtoDemuxer.h"

#include <string>
#include <sstream>

#include <string.h>

using namespace kmsvr;
using namespace kuma;

const uint32_t kTimeoutIntervalMS = 30000;


ProtoDemuxer::ProtoDemuxer(const RunLoop::Ptr &loop)
: loop_(loop.get())
, tcp_(loop->getEventLoop().get())
, timer_(loop->getEventLoop().get())
{
    
}

ProtoDemuxer::~ProtoDemuxer()
{
    close();
}

KMError ProtoDemuxer::attachFd(SOCKET_FD fd, uint32_t ssl_flags, ProtoCallback scb, ErrorCallback ecb)
{
    scb_ = std::move(scb);
    ecb_ = std::move(ecb);
    http_parser_.setDataCallback([this] (KMBuffer &buf) { onHttpData(buf); });
    http_parser_.setEventCallback([this] (HttpEvent ev) { onHttpEvent(ev); });
    
    tcp_.setWriteCallback([this] (KMError err) { onSend(err); });
    tcp_.setReadCallback([this] (KMError err) { onReceive(err); });
    tcp_.setErrorCallback([this] (KMError err) { onClose(err); });
    tcp_.setSslFlags(ssl_flags);
    auto ret = tcp_.attachFd(fd);
    if (ret == KMError::NOERR) {
        timer_.schedule(kTimeoutIntervalMS, kuma::Timer::Mode::ONE_SHOT, [this] {
            onError(0, "timeout");
        });
    }
    return ret;
}

void ProtoDemuxer::close()
{
    timer_.cancel();
    tcp_.close();
    http_parser_.reset();
}

void ProtoDemuxer::onHttpData(KMBuffer &buf)
{
    printf("ProtoDemuxer::onHttpData, len=%zu\n", buf.chainLength());
}

void ProtoDemuxer::onHttpEvent(HttpEvent ev)
{
    switch (ev) {
        case HttpEvent::HEADER_COMPLETE: {
            http_parser_.pause();
            break;
        }
        case HttpEvent::HTTP_ERROR: {
            onError(0, "HTTP error");
            break;
        }
            
        default:
            break;
    }
}

bool ProtoDemuxer::checkHttp2()
{
    if (tcp_.sslEnabled()) {
        char buf[64];
        auto ret = tcp_.getAlpnSelected(buf, sizeof(buf));
        if (ret == KMError::NOERR && strcmp("h2", buf) == 0) { // HTTP/2.0
            onProto(Proto::HTTP2, nullptr);
            return true;
        }
    }
    return false;
}

void ProtoDemuxer::demuxHttp(const KMBuffer *init_buf)
{
    Proto proto = Proto::HTTP;
    if (http_parser_.isUpgradeTo("WebSocket")) {
        proto = Proto::WebSocket;
    } else if (http_parser_.isUpgradeTo("h2c")) {
        proto = Proto::HTTP2;
    }
    onProto(proto, init_buf);
}

void ProtoDemuxer::onSend(KMError err)
{
    checkHttp2();
}

void ProtoDemuxer::onReceive(KMError err)
{
    if (checkHttp2()) {
        return;
    }
    char buf[4096] = {0};
    do
    {
        int bytes_read = tcp_.receive((uint8_t*)buf, sizeof(buf));
        if(bytes_read < 0) {
            tcp_.close();
            onError(bytes_read, "receive error");
            return ;
        } else if (0 == bytes_read){
            break;
        }
        DESTROY_DETECTOR_SETUP();
        int bytes_used = http_parser_.parse(buf, bytes_read);
        DESTROY_DETECTOR_CHECK_VOID();
        if (http_parser_.headerComplete()) {
            auto init_len = bytes_read - bytes_used;
            KMBuffer kmb(buf + bytes_used, init_len, init_len);
            demuxHttp(&kmb);
            break;
        }
    } while(true);
}

void ProtoDemuxer::onClose(KMError err)
{
    printf("ProtoDemuxer::onClose, err=%d\n", (int)err);
    onError((int)err, "socket error");
}

void ProtoDemuxer::onProto(Proto proto, const KMBuffer *init_buf)
{
    timer_.cancel();
    if (scb_) {
        scb_(proto, std::move(tcp_), std::move(http_parser_), init_buf);
    }
    loop_->removeObject(getObjectId());
}

void ProtoDemuxer::onError(int errcode, const std::string &errmsg)
{
    timer_.cancel();
    tcp_.close();
    if (ecb_) {
        ecb_(errcode, errmsg);
    }
    loop_->removeObject(getObjectId());
}
