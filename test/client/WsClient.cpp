#include "WsClient.h"

#if defined(KUMA_OS_WIN)
# include <Ws2tcpip.h>
#else
# include <arpa/inet.h>
#endif

uint32_t getSendInterval();

WsClient::WsClient(EventLoop* loop, long conn_id, TestLoop* server)
: loop_(loop)
, ws_(loop_)
, timed_sending_(false)
, timer_(loop_)
, server_(server)
, conn_id_(conn_id)
, index_(0)
, max_send_count_(200000)
{
    
}

void WsClient::startRequest(std::string& url)
{
    ws_.setDataCallback([this] (uint8_t* data, size_t len) { onData(data, len); });
    ws_.setWriteCallback([this] (int err) { onSend(err); });
    ws_.setErrorCallback([this] (int err) { onClose(err); });
    //timer_.schedule(1000, [this] { onTimer(); }, true);
    ws_.setProtocol("kuma");
    ws_.setOrigin("www.kuma.com");
    ws_.connect(url.c_str(), [this] (int err) { onConnect(err); });
}

int WsClient::close()
{
    timer_.cancel();
    return ws_.close();
}

void WsClient::sendData()
{
    uint8_t buf[1024];
    *(uint32_t*)buf = htonl(++index_);
    ws_.send(buf, sizeof(buf));
}

void WsClient::onConnect(int err)
{
    printf("WsClient::onConnect, err=%d\n", err);
    start_point_ = std::chrono::steady_clock::now();
    sendData();
    if (getSendInterval() > 0) {
        timed_sending_ = true;
        timer_.schedule(getSendInterval(), [this] { sendData(); }, TimerMode::REPEATING);
    }
}

void WsClient::onSend(int err)
{
    //printf("Client::onSend\n");
}

void WsClient::onData(uint8_t* data, size_t len)
{
    uint32_t index = 0;
    if(len >= 4) {
        index = ntohl(*(uint32_t*)data);
    }
    if(index % 10000 == 0) {
        printf("WsClient::onReceive, bytes_read=%zu, index=%d\n", len, index);
    }
    if(index < max_send_count_) {
        if (!timed_sending_) {
            sendData();
        }
    } else {
        timer_.cancel();
        std::chrono::steady_clock::time_point end_point = std::chrono::steady_clock::now();
        std::chrono::milliseconds diff_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end_point - start_point_);
        printf("spent %lld ms to echo %u packets\n", diff_ms.count(), max_send_count_);
        server_->removeObject(conn_id_);
    }
}

void WsClient::onClose(int err)
{
    printf("WsClient::onClose, err=%d\n", err);
    server_->removeObject(conn_id_);
}
