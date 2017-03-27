#include "TcpClient.h"

#if defined(KUMA_OS_WIN)
# include <Ws2tcpip.h>
#else
# include <arpa/inet.h>
#endif

TcpClient::TcpClient(TestLoop* loop, long conn_id)
: loop_(loop)
, tcp_(loop->eventLoop())
, timer_(loop->eventLoop())
, conn_id_(conn_id)
, index_(0)
, max_send_count_(200000)
{
    
}

KMError TcpClient::bind(const char* bind_host, uint16_t bind_port)
{
    return tcp_.bind(bind_host, bind_port);
}

KMError TcpClient::connect(const char* host, uint16_t port)
{
    tcp_.setReadCallback([this] (KMError err) { onReceive(err); });
    tcp_.setWriteCallback([this] (KMError err) { onSend(err); });
    tcp_.setErrorCallback([this] (KMError err) { onClose(err); });
    timer_.schedule(1000, [this] { onTimer(); }, TimerMode::REPEATING);
    return tcp_.connect(host, port, [this] (KMError err) { onConnect(err); });
}

int TcpClient::close()
{
    timer_.cancel();
    tcp_.close();
    return 0;
}

void TcpClient::sendData()
{
    uint8_t buf[1024];
    *(uint32_t*)buf = htonl(++index_);
    tcp_.send(buf, sizeof(buf));
    // should buffer remain data if send length < sizeof(buf)
}

void TcpClient::onConnect(KMError err)
{
    printf("TcpClient::onConnect, err=%d\n", err);
    start_point_ = std::chrono::steady_clock::now();
    sendData();
}

void TcpClient::onSend(KMError err)
{
    //printf("TcpClient::onSend\n");
}

void TcpClient::onReceive(KMError err)
{
    char buf[4096] = {0};
    do {
        int bytes_read = tcp_.receive((uint8_t*)buf, sizeof(buf));
        if(bytes_read > 0) {
            uint32_t index = 0;
            if(bytes_read >= 4) {
                index = ntohl(*(uint32_t*)buf);
            }
            if(index % 10000 == 0) {
                printf("TcpClient::onReceive, bytes_read=%d, index=%d\n", bytes_read, index);
            }
            if(index < max_send_count_) {
                sendData();
            } else {
                timer_.cancel();
                std::chrono::steady_clock::time_point end_point = std::chrono::steady_clock::now();
                std::chrono::milliseconds diff_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end_point - start_point_);
                printf("spent %lld ms to echo %u packets\n", diff_ms.count(), max_send_count_);
            }
        } else if (0 == bytes_read) {
            break;
        } else {
            printf("TcpClient::onReceive, err=%d\n", getLastError());
            break;
        }
    } while (true);
}

void TcpClient::onClose(KMError err)
{
    printf("TcpClient::onClose, err=%d\n", err);
    timer_.cancel();
    tcp_.close();
    loop_->removeObject(conn_id_);
}

void TcpClient::onTimer()
{
    printf("TcpClient::onTimer\n");
}
