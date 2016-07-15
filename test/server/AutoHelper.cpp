#include "AutoHelper.h"
#include "TestLoop.h"

AutoHelper::AutoHelper(EventLoop* loop, long conn_id, TestLoop* server)
: loop_(loop)
, server_(server)
, conn_id_(conn_id)
, tcp_(loop_)
, destroy_flag_ptr_(nullptr)
{
    
}

AutoHelper::~AutoHelper()
{
    if(destroy_flag_ptr_) {
        *destroy_flag_ptr_ = true;
    }
}

int AutoHelper::attachFd(SOCKET_FD fd, uint32_t ssl_flags)
{
    http_parser_.setDataCallback([this] (const char* data, size_t len) { onHttpData(data, len); });
    http_parser_.setEventCallback([this] (HttpEvent ev) { onHttpEvent(ev); });
    
    tcp_.setReadCallback([this] (int err) { onReceive(err); });
    tcp_.setErrorCallback([this] (int err) { onClose(err); });
    tcp_.setSslFlags(ssl_flags);
    return tcp_.attachFd(fd);
}

int AutoHelper::close()
{
    tcp_.close();
    http_parser_.reset();
    return 0;
}

void AutoHelper::onHttpData(const char* data, size_t len)
{
    printf("AutoHelper::onHttpData, len=%u\n", len);
}

void AutoHelper::onHttpEvent(HttpEvent ev)
{
    printf("AutoHelper::onHttpEvent, ev=%u\n", ev);
    switch (ev) {
        case HTTP_HEADER_COMPLETE:
        {
            http_parser_.pause();
            server_->addTcp(std::move(tcp_), std::move(http_parser_));
            server_->removeObject(conn_id_);
            break;
        }
        case HTTP_ERROR:
        {
            server_->removeObject(conn_id_);
            break;
        }
            
        default:
            break;
    }
}

void AutoHelper::onReceive(int err)
{
    char buf[4096] = {0};
    do
    {
        int bytes_read = tcp_.receive((uint8_t*)buf, sizeof(buf));
        if(bytes_read < 0) {
            tcp_.close();
            server_->removeObject(conn_id_);
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
            printf("AutoHelper::onReceive, bytes_used=%u, bytes_read=%un", bytes_used, bytes_read);
        }
    } while(true);
}

void AutoHelper::onClose(int err)
{
    printf("AutoHelper::onClose, err=%d\n", err);
    server_->removeObject(conn_id_);
}
