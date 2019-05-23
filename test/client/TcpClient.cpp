#include "TcpClient.h"

#if defined(KUMA_OS_WIN)
# include <Ws2tcpip.h>
#else
# include <arpa/inet.h>
#endif

#define TEST_PROXY_CONNECTION

extern std::string g_proxy_url;
extern std::string g_proxy_user;
extern std::string g_proxy_passwd;

TcpClient::TcpClient(TestLoop* loop, long conn_id)
: loop_(loop)
, tcp_(loop->eventLoop())
, proxy_conn_(loop->eventLoop())
, timer_(loop->eventLoop())
, conn_id_(conn_id)
, index_(0)
, max_send_count_(200000)
{
    tcp_.setReadCallback([this] (KMError err) { onReceive(err); });
    tcp_.setWriteCallback([this] (KMError err) { onSend(err); });
    tcp_.setErrorCallback([this] (KMError err) { onClose(err); });

    proxy_conn_.setDataCallback([this] (uint8_t *data, size_t size) { return onData(data, size); });
    proxy_conn_.setWriteCallback([this] (KMError err) { onSend(err); });
    proxy_conn_.setErrorCallback([this] (KMError err) { onClose(err); });
}

KMError TcpClient::bind(const std::string &bind_host, uint16_t bind_port)
{
    return tcp_.bind(bind_host.c_str(), bind_port);
}

KMError TcpClient::connect(const std::string &host, uint16_t port)
{
    if (g_proxy_url.empty()) {
        timer_.schedule(1000, TimerMode::REPEATING, [this] { onTimer(); });
        return tcp_.connect(host.c_str(), port, [this] (KMError err) { onConnect(err); });
    } else {
        proxy_conn_.setProxyInfo(g_proxy_url.c_str(), g_proxy_user.c_str(), g_proxy_passwd.c_str());
        proxy_conn_.setSslFlags(ssl_flags_ | SSL_ALLOW_SELF_SIGNED_CERT);
        return proxy_conn_.connect(host.c_str(), port, [this] (KMError err) { onConnect(err); });
    }
}

int TcpClient::close()
{
    timer_.cancel();
    tcp_.close();
    proxy_conn_.close();
    return 0;
}

void TcpClient::sendData()
{
    uint8_t buf[1024];
    *(uint32_t*)buf = htonl(++index_);
    if (g_proxy_url.empty()) {
        tcp_.send(buf, sizeof(buf));
    } else {
        proxy_conn_.send(buf, sizeof(buf));
    }
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

KMError TcpClient::onData(uint8_t *data, size_t size)
{
    printf("TcpClient::onData, size=%zu\n", size);
    return KMError::NOERR;
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
