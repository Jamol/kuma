#include "HttpClient.h"

#include <string.h> // for memset
#include <atomic>

extern std::string getHttpVersion();
HttpClient::HttpClient(TestLoop* loop, long conn_id)
: loop_(loop)
, http_request_(loop->eventLoop(), getHttpVersion().c_str())
, conn_id_(conn_id)
{
    
}

void HttpClient::startRequest(const std::string& url)
{
    http_request_.setDataCallback([this] (void* data, size_t len) { onData(data, len); });
    http_request_.setWriteCallback([this] (KMError err) { onSend(err); });
    http_request_.setErrorCallback([this] (KMError err) { onClose(err); });
    http_request_.setHeaderCompleteCallback([this] { onHeaderComplete(); });
    http_request_.setResponseCompleteCallback([this] { onRequestComplete(); });
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

void HttpClient::onData(void* data, size_t len)
{
    total_bytes_read_ += len;
    //printf("HttpClient_%ld::onData, len=%zu, total=%zu\n", conn_id_, len, total_bytes_read_);
}

void HttpClient::onSend(KMError err)
{
    uint8_t buf[16*1024];
    memset(buf, 'a', sizeof(buf));
    while (true) {
        int ret = http_request_.sendData(buf, sizeof(buf));
        if (ret < 0) {
            break;
        } else if (ret < sizeof(buf)) {
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
    printf("HttpClient_%ld::onRequestComplete, total=%zu, count=%d\n", conn_id_, total_bytes_read_, ++req_count);
    http_request_.close();
}
