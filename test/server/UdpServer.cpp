#include "UdpServer.h"

UdpServer::UdpServer(EventLoop* loop)
: loop_(loop)
, udp_(loop_)
{
    
}

KMError UdpServer::bind(const char* host, uint16_t port)
{
    udp_.setReadCallback([this] (KMError err) { onReceive(err); });
    udp_.setErrorCallback([this] (KMError err) { onClose(err); });
    return udp_.bind(host, port);
}

int UdpServer::close()
{
    udp_.close();
    return 0;
}

void UdpServer::onReceive(KMError err)
{
    char buf[4096] = {0};
    char ip[128];
    uint16_t port = 0;
    do {
        int bytes_read = udp_.receive((uint8_t*)buf, sizeof(buf), ip, sizeof(ip), port);
        if(bytes_read < 0) {
            udp_.close();
            return ;
        } else if(0 == bytes_read) {
            break;
        }
        int ret = udp_.send((uint8_t*)buf, bytes_read, ip, port);
        if(ret < 0) {
            udp_.close();
        }
    } while(true);
}

void UdpServer::onClose(KMError err)
{
    printf("UdpServer::onClose, err=%d\n", err);
}
