#include "WsClient.h"

#if defined(KUMA_OS_WIN)
# include <Ws2tcpip.h>
#else
# include <arpa/inet.h>
#endif

extern std::string g_proxy_url;
extern std::string g_proxy_user;
extern std::string g_proxy_passwd;

uint32_t getSendInterval();

extern std::string getHttpVersion();
WsClient::WsClient(TestLoop* loop, long conn_id)
: loop_(loop)
, ws_(loop->eventLoop(), getHttpVersion().c_str())
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
    ws_.setOpenCallback([this] (KMError err) { onOpen(err); });
    ws_.setDataCallback([this] (KMBuffer &buf, bool is_text, bool fin) {
        onData(buf);
    });
    ws_.setWriteCallback([this] (KMError err) { onSend(err); });
    ws_.setErrorCallback([this] (KMError err) { onClose(err); });
    ws_.setProxyInfo(g_proxy_url.c_str(), g_proxy_user.c_str(), g_proxy_passwd.c_str());
    ws_.setSubprotocol("jws");
    ws_.setOrigin("www.jamol.cn");
    ws_.addHeader("x-forward-addr", "123");
    ws_.addHeader("x-custom", "kmtest");
    ws_.connect(url.c_str());
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
    buf[0] = 12;
	for (int i = 1; i < sizeof(buf); ++i) {
        buf[i] = buf[i-1] + 1;
    }
    *(uint32_t*)buf = htonl(++index_);
    //ws_.send(buf, sizeof(buf), false);
    KMBuffer kmb(buf, sizeof(buf), sizeof(buf));
    ws_.send(kmb, false);
    // should buffer remain data if send length < sizeof(buf)
}

void WsClient::onOpen(KMError err)
{
    printf("WsClient::onOpen, err=%d\n", err);
    start_point_ = std::chrono::steady_clock::now();
    sendData();
    if (getSendInterval() > 0) {
        timed_sending_ = true;
        timer_.schedule(getSendInterval(), Timer::Mode::REPEATING, [this] { sendData(); });
    }
}

void WsClient::onSend(KMError err)
{
    //printf("Client::onSend\n");
}

void WsClient::onData(KMBuffer &buf)
{
    auto chain_len = buf.chainLength();
    uint32_t index = 0;
    if(chain_len >= 4) {
        uint8_t d[4];
        buf.readChained(d, 4);
        index = ntohl(*(uint32_t*)d);
    }
    if(index % 1000 == 0) {
        printf("WsClient::onReceive, bytes_read=%zu, index=%d\n", chain_len, index);
    }
    if(index < max_send_count_) {
        if (!timed_sending_) {
            sendData();
        }
    } else {
        timer_.cancel();
        std::chrono::steady_clock::time_point end_point = std::chrono::steady_clock::now();
        std::chrono::milliseconds diff_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end_point - start_point_);
        printf("spent %lld ms to echo %u packets\n", (long long int)diff_ms.count(), max_send_count_);
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
