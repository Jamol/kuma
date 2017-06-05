#include "WsClient.h"

#if defined(KUMA_OS_WIN)
# include <Ws2tcpip.h>
#else
# include <arpa/inet.h>
#endif

uint32_t getSendInterval();

WsClient::WsClient(TestLoop* loop, long conn_id)
: loop_(loop)
, ws_(loop->eventLoop())
, timed_sending_(false)
, timer_(loop->eventLoop())
, conn_id_(conn_id)
, index_(0)
, max_send_count_(200000)
{
    
}

void WsClient::startRequest(const std::string& url)
{
    ws_.setSslFlags(SSL_ALLOW_SELF_SIGNED_CERT);
    ws_.setDataCallback([this] (void* data, size_t len, bool is_text, bool fin) {
        onData(data, len);
    });
    ws_.setWriteCallback([this] (KMError err) { onSend(err); });
    ws_.setErrorCallback([this] (KMError err) { onClose(err); });
    //timer_.schedule(1000, [this] { onTimer(); }, true);
    ws_.setProtocol("jws");
    ws_.setOrigin("www.jamol.cn");
    ws_.connect(url.c_str(), [this] (KMError err) { onConnect(err); });
}

int WsClient::close()
{
    timer_.cancel();
    ws_.close();
    return 0;
}

void WsClient::sendData()
{
    uint8_t buf[1024];
    *(uint32_t*)buf = htonl(++index_);
    ws_.send(buf, sizeof(buf), false);
    // should buffer remain data if send length < sizeof(buf)
}

void WsClient::onConnect(KMError err)
{
    printf("WsClient::onConnect, err=%d\n", err);
    start_point_ = std::chrono::steady_clock::now();
    sendData();
    if (getSendInterval() > 0) {
        timed_sending_ = true;
        timer_.schedule(getSendInterval(), [this] { sendData(); }, TimerMode::REPEATING);
    }
}

void WsClient::onSend(KMError err)
{
    //printf("Client::onSend\n");
}

void WsClient::onData(void* data, size_t len)
{
    uint32_t index = 0;
    if(len >= 4) {
        index = ntohl(*(uint32_t*)data);
    }
    if(index % 1000 == 0) {
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
        loop_->removeObject(conn_id_);
    }
}

void WsClient::onClose(KMError err)
{
    printf("WsClient::onClose, err=%d\n", err);
    timer_.cancel();
    ws_.close();
    loop_->removeObject(conn_id_);
}
