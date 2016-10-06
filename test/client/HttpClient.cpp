#include "HttpClient.h"

extern std::string getHttpVersion();
HttpClient::HttpClient(TestLoop* loop, long conn_id)
: loop_(loop)
, http_request_(loop->getEventLoop(), getHttpVersion().c_str())
, total_bytes_read_(0)
, conn_id_(conn_id)
{
    
}

void HttpClient::startRequest(std::string& url)
{
    http_request_.setDataCallback([this] (void* data, size_t len) { onData(data, len); });
    http_request_.setWriteCallback([this] (KMError err) { onSend(err); });
    http_request_.setErrorCallback([this] (KMError err) { onClose(err); });
    http_request_.setHeaderCompleteCallback([this] { onHeaderComplete(); });
    http_request_.setResponseCompleteCallback([this] { onRequestComplete(); });
    http_request_.sendRequest("GET", url.c_str());
}

int HttpClient::close()
{
    http_request_.close();
    return 0;
}

void HttpClient::onData(void* data, size_t len)
{
    std::string str((char*)data, len);
    total_bytes_read_ += len;
    printf("HttpClient_%ld::onData, len=%zu, total=%zu\n", conn_id_, len, total_bytes_read_);
}

void HttpClient::onSend(KMError err)
{
    
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
    printf("HttpClient_%ld::onRequestComplete, total=%zu\n", conn_id_, total_bytes_read_);
    http_request_.close();
}
