#include "HttpClient.h"

#include <string.h> // for memset
#include <atomic>

extern std::string g_proxy_url;
extern std::string g_proxy_user;
extern std::string g_proxy_passwd;

extern std::string getHttpVersion();

HttpClient::HttpClient(TestLoop* loop, long conn_id)
: loop_(loop)
, http_request_(loop->eventLoop(), getHttpVersion().c_str())
, conn_id_(conn_id)
{
    
}

void HttpClient::startRequest(const std::string& url)
{
    url_ = url;
    http_request_.setSslFlags(SSL_ALLOW_SELF_SIGNED_CERT);
    http_request_.setDataCallback([this] (KMBuffer &buf) { onData(buf); });
    http_request_.setWriteCallback([this] (KMError err) { onSend(err); });
    http_request_.setErrorCallback([this] (KMError err) { onClose(err); });
    http_request_.setHeaderCompleteCallback([this] { onHeaderComplete(); });
    http_request_.setResponseCompleteCallback([this] { onRequestComplete(); });
    http_request_.setProxyInfo(g_proxy_url.c_str(), g_proxy_user.c_str(), g_proxy_passwd.c_str());
    if (url.find("/testdata") != std::string::npos) {
        http_request_.addHeader("Content-Length", 128*1024*1024);
        http_request_.sendRequest("POST", url.c_str());
    } else {
        http_request_.sendRequest("GET", url.c_str());
    }
}

int HttpClient::close()
{
    http_request_.close();
    return 0;
}

void HttpClient::onData(KMBuffer &buf)
{
    total_bytes_read_ += buf.chainLength();
    //printf("HttpClient_%ld::onData, len=%zu, total=%zu\n", conn_id_, buf.chainLength(), total_bytes_read_);
}

void HttpClient::onSend(KMError err)
{
    const size_t kBufferSize = 16*1024;
    uint8_t buf[kBufferSize];
    //memset(buf, 'a', sizeof(buf));
    buf[0] = 112;
	for (int i = 1; i < sizeof(buf); ++i) {
        buf[i] = buf[i-1] + 1;
    }
    KMBuffer buf1(buf, kBufferSize/2, kBufferSize/2);
    KMBuffer buf2(buf + kBufferSize/2, kBufferSize/2, kBufferSize/2);
    buf1.append(&buf2);
    while (true) {
        int ret = http_request_.sendData(buf1);
        if (ret < 0) {
            break;
        } else if (ret < (int)buf1.chainLength()) {
            // should buffer remain data
            break;
        }
    }
}

void HttpClient::onClose(KMError err)
{
    printf("HttpClient_%ld::onClose, err=%d\n", conn_id_, err);
    http_request_.close();
    loop_->removeObject(conn_id_);
}

void HttpClient::onHeaderComplete()
{
    printf("HttpClient_%ld::onHeaderComplete\n", conn_id_);
}

void HttpClient::onRequestComplete()
{
    static std::atomic_int req_count{0};
    printf("HttpClient_%ld::onRequestComplete, status=%d, total=%zu, count=%d\n",
           conn_id_, http_request_.getStatusCode(), total_bytes_read_, ++req_count);
    
    if (req_count == 1 && test_reuse_) {
        // test connection reuse
        total_bytes_read_ = 0;
        if (url_.find("/testdata") != std::string::npos) {
            http_request_.addHeader("Content-Length", 128*1024*1024);
            http_request_.sendRequest("POST", url_.c_str());
        } else {
            http_request_.sendRequest("GET", url_.c_str());
        }
    } else {
        http_request_.close();
    }
}
