#include "UdpClient.h"

#if defined(KUMA_OS_WIN)
# include <Ws2tcpip.h>
#else
# include <arpa/inet.h>
#endif

UdpClient::UdpClient(TestLoop* loop, long conn_id)
: loop_(loop)
, udp_(loop->getEventLoop())
, conn_id_(conn_id)
, index_(0)
, max_send_count_(200000)
{
    
}

KMError UdpClient::bind(const char* bind_host, uint16_t bind_port)
{
    udp_.setReadCallback([this] (KMError err) { onReceive(err); });
    udp_.setErrorCallback([this] (KMError err) { onClose(err); });
    return udp_.bind(bind_host, bind_port);
}

int UdpClient::close()
{
    udp_.close();
    return 0;
}

void UdpClient::startSend(const char* host, uint16_t port)
{
    host_ = host;
    port_ = port;
    start_point_ = std::chrono::steady_clock::now();
    sendData();
}

void UdpClient::sendData()
{
    uint8_t buf[1024];
    *(uint32_t*)buf = htonl(++index_);
    udp_.send(buf, sizeof(buf), host_.c_str(), port_);
}

void UdpClient::onReceive(KMError err)
{
    char buf[4096] = {0};
    char ip[128];
    uint16_t port = 0;
    do {
        int bytes_read = udp_.receive((uint8_t*)buf, sizeof(buf), ip, sizeof(ip), port);
        if(bytes_read > 0) {
            uint32_t index = 0;
            if(bytes_read >= 4) {
                index = ntohl(*(uint32_t*)buf);
            }
            if(index % 10000 == 0) {
                printf("UdpClient::onReceive, bytes_read=%d, index=%d\n", bytes_read, index);
            }
            if(index < max_send_count_) {
                sendData();
            } else {
                std::chrono::steady_clock::time_point end_point = std::chrono::steady_clock::now();
                std::chrono::milliseconds diff_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end_point - start_point_);
                printf("spent %lld ms to echo %u packets\n", diff_ms.count(), max_send_count_);
            }
        } else if (0 == bytes_read) {
            break;
        } else {
            printf("UdpClient::onReceive, err=%d\n", getLastError());
        }
    } while (0);
}

void UdpClient::onClose(KMError err)
{
    printf("UdpClient::onClose, err=%d\n", err);
    loop_->removeObject(conn_id_);
}
