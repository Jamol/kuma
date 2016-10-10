#include "ProtoDemuxer.h"
#include "TestLoop.h"
#include <string>
#include <sstream>

#include <string.h>

#ifdef KUMA_OS_WIN
# define snprintf       _snprintf
# define vsnprintf      _vsnprintf
# define strcasecmp     _stricmp
# define strncasecmp    _strnicmp
#endif

char* trim_left(char* str);
char* trim_right(char* str);
char* trim_right(char* str, char* str_end);
std::string& trim_left(std::string& str);
std::string& trim_right(std::string& str);

ProtoDemuxer::ProtoDemuxer(TestLoop* loop, long conn_id)
: loop_(loop)
, conn_id_(conn_id)
, tcp_(loop->getEventLoop())
{
    
}

ProtoDemuxer::~ProtoDemuxer()
{
    
}

KMError ProtoDemuxer::attachFd(SOCKET_FD fd, uint32_t ssl_flags)
{
    http_parser_.setDataCallback([this] (void* data, size_t len) { onHttpData(data, len); });
    http_parser_.setEventCallback([this] (HttpEvent ev) { onHttpEvent(ev); });
    
    tcp_.setWriteCallback([this] (KMError err) { onSend(err); });
    tcp_.setReadCallback([this] (KMError err) { onReceive(err); });
    tcp_.setErrorCallback([this] (KMError err) { onClose(err); });
    tcp_.setSslFlags(ssl_flags);
    return tcp_.attachFd(fd);
}

int ProtoDemuxer::close()
{
    tcp_.close();
    http_parser_.reset();
    return 0;
}

void ProtoDemuxer::onHttpData(void* data, size_t len)
{
    printf("ProtoDemuxer::onHttpData, len=%zu\n", len);
}

void ProtoDemuxer::onHttpEvent(HttpEvent ev)
{
    printf("ProtoDemuxer::onHttpEvent, ev=%u\n", ev);
    switch (ev) {
        case HttpEvent::HEADER_COMPLETE:
        {
            http_parser_.pause();
            break;
        }
        case HttpEvent::HTTP_ERROR:
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
        auto ret = tcp_.getAlpnSelected(buf, sizeof(buf));
        if (ret == KMError::NOERR && strcmp("h2", buf) == 0) { // HTTP/2.0
            loop_->addH2Conn(std::move(tcp_), std::move(http_parser_), nullptr, 0);
            loop_->removeObject(conn_id_);
            return true;
        }
    }
    return false;
}

void ProtoDemuxer::demuxHttp(void *init_data, size_t init_len)
{
    if (http_parser_.isUpgradeTo("WebSocket")) {
        loop_->addWebSocket(std::move(tcp_), std::move(http_parser_), init_data, init_len);
    } else if (http_parser_.isUpgradeTo("h2c")) {
        loop_->addH2Conn(std::move(tcp_), std::move(http_parser_), init_data, init_len);
    } else {
        loop_->addHttp(std::move(tcp_), std::move(http_parser_), init_data, init_len);
    }
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
            loop_->removeObject(conn_id_);
            return ;
        } else if (0 == bytes_read){
            break;
        }
        DESTROY_DETECTOR_SETUP();
        int bytes_used = http_parser_.parse(buf, bytes_read);
        DESTROY_DETECTOR_CHECK_VOID();
        if (http_parser_.headerComplete()) {
            demuxHttp(buf + bytes_used, bytes_read - bytes_used);
            loop_->removeObject(conn_id_);
            break;
        }
    } while(true);
}

void ProtoDemuxer::onClose(KMError err)
{
    printf("ProtoDemuxer::onClose, err=%d\n", err);
    loop_->removeObject(conn_id_);
}

char* trim_left(char* str)
{
    while (*str && isspace(*str++)) {
        ;
    }
    
    return str;
}

char* trim_right(char* str)
{
    return trim_right(str, str + strlen(str));
}

char* trim_right(char* str, char* str_end)
{
    while (--str_end >= str && isspace(*str_end)) {
        ;
    }
    *(++str_end) = 0;
    
    return str;
}

std::string& trim_left(std::string& str)
{
    str.erase(0, str.find_first_not_of(' '));
    return str;
}

std::string& trim_right(std::string& str)
{
    auto pos = str.find_last_not_of(' ');
    if(pos != std::string::npos) {
        str.erase(pos + 1);
    }
    return str;
}
