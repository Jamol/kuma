#include "Client.h"

#if defined(KUMA_OS_WIN)
# include <Ws2tcpip.h>
#else
# include <arpa/inet.h>
#endif

Client::Client(EventLoop* loop, long conn_id, TestLoop* server)
: loop_(loop)
, tcp_(loop_)
, timer_(loop_)
, server_(server)
, conn_id_(conn_id)
, index_(0)
, max_send_count_(200000)
{
    
}

int Client::bind(const char* bind_host, uint16_t bind_port)
{
    return tcp_.bind(bind_host, bind_port);
}

int Client::connect(const char* host, uint16_t port)
{
    tcp_.setReadCallback([this] (int err) { onReceive(err); });
    tcp_.setWriteCallback([this] (int err) { onSend(err); });
    tcp_.setErrorCallback([this] (int err) { onClose(err); });
    timer_.schedule(1000, [this] { onTimer(); }, true);
    return tcp_.connect(host, port, [this] (int err) { onConnect(err); });
}

int Client::close()
{
    timer_.cancel();
    return tcp_.close();
}

void Client::sendData()
{
    uint8_t buf[1024];
    *(uint32_t*)buf = htonl(++index_);
    tcp_.send(buf, sizeof(buf));
}

void Client::onConnect(int err)
{
    printf("Client::onConnect, err=%d\n", err);
    start_point_ = std::chrono::steady_clock::now();
    sendData();
}

void Client::onSend(int err)
{
    //printf("Client::onSend\n");
}

void Client::onReceive(int err)
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
                printf("Client::onReceive, bytes_read=%d, index=%d\n", bytes_read, index);
            }
            if(index < max_send_count_) {
                sendData();
            } else {
                timer_.cancel();
                std::chrono::steady_clock::time_point end_point = std::chrono::steady_clock::now();
                std::chrono::milliseconds diff_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end_point - start_point_);
                printf("spent %u ms to echo %u packets\n", diff_ms.count(), max_send_count_);
            }
        } else if (0 == bytes_read) {
            break;
        } else {
            printf("Client::onReceive, err=%d\n", getLastError());
        }
    } while (true);
}

void Client::onClose(int err)
{
    printf("Client::onClose, err=%d\n", err);
    server_->removeObject(conn_id_);
}

void Client::onTimer()
{
    printf("Client::onTimer\n");
}
