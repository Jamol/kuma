#include "ProtoDemuxer.h"
#include "TestLoop.h"

ProtoDemuxer::ProtoDemuxer(TestLoop* loop, long conn_id)
: loop_(loop)
, conn_id_(conn_id)
, tcp_(loop->getEventLoop())
, destroy_flag_ptr_(nullptr)
{
    
}

ProtoDemuxer::~ProtoDemuxer()
{
    if(destroy_flag_ptr_) {
        *destroy_flag_ptr_ = true;
    }
}

int ProtoDemuxer::attachFd(SOCKET_FD fd, uint32_t ssl_flags)
{
    http_parser_.setDataCallback([this] (const char* data, size_t len) { onHttpData(data, len); });
    http_parser_.setEventCallback([this] (HttpEvent ev) { onHttpEvent(ev); });
    
    tcp_.setWriteCallback([this] (int err) { onSend(err); });
    tcp_.setReadCallback([this] (int err) { onReceive(err); });
    tcp_.setErrorCallback([this] (int err) { onClose(err); });
    tcp_.setSslFlags(ssl_flags);
    return tcp_.attachFd(fd);
}

int ProtoDemuxer::close()
{
    tcp_.close();
    http_parser_.reset();
    return 0;
}

void ProtoDemuxer::onHttpData(const char* data, size_t len)
{
    printf("ProtoDemuxer::onHttpData, len=%zu\n", len);
}

void ProtoDemuxer::onHttpEvent(HttpEvent ev)
{
    printf("ProtoDemuxer::onHttpEvent, ev=%u\n", ev);
    switch (ev) {
        case HTTP_HEADER_COMPLETE:
        {
            http_parser_.pause();
            demuxHttp();
            loop_->removeObject(conn_id_);
            break;
        }
        case HTTP_ERROR:
        {
            loop_->removeObject(conn_id_);
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
        int ret = tcp_.getAlpnSelected(buf, sizeof(buf));
        if (ret == KUMA_ERROR_NOERR && strcmp("h2", buf)) { // HTTP/2.0
            loop_->addHttp2(std::move(tcp_), std::move(http_parser_));
            loop_->removeObject(conn_id_);
            return true;
        }
    }
    return false;
}

void ProtoDemuxer::demuxHttp()
{
    if (strcasecmp(http_parser_.getHeaderValue("Connection"), "Upgrade") == 0) {
        if (strcasecmp(http_parser_.getHeaderValue("Upgrade"), "WebSocket") == 0) {
            loop_->addWebSocket(std::move(tcp_), std::move(http_parser_));
        } else if (strcasecmp(http_parser_.getHeaderValue("Upgrade"), "h2c") == 0) {
            loop_->addHttp2(std::move(tcp_), std::move(http_parser_));
        }
    } else {
        loop_->addHttp(std::move(tcp_), std::move(http_parser_));
    }
}

void ProtoDemuxer::onSend(int err)
{
    checkHttp2();
}

void ProtoDemuxer::onReceive(int err)
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
            loop_->removeObject(conn_id_);
            return ;
        } else if (0 == bytes_read){
            break;
        }
        bool destroyed = false;
        destroy_flag_ptr_ = &destroyed;
        int bytes_used = http_parser_.parse(buf, bytes_read);
        if(destroyed) {
            return;
        }
        destroy_flag_ptr_ = nullptr;
        if(bytes_used != bytes_read) {
            printf("ProtoDemuxer::onReceive, bytes_used=%u, bytes_read=%un", bytes_used, bytes_read);
        }
    } while(true);
}

void ProtoDemuxer::onClose(int err)
{
    printf("ProtoDemuxer::onClose, err=%d\n", err);
    loop_->removeObject(conn_id_);
}
