#include "HttpTest.h"
#include "TestLoop.h"

HttpTest::HttpTest(EventLoop* loop, long conn_id, TestLoop* server)
: loop_(loop)
, http_(loop_)
, server_(server)
, conn_id_(conn_id)
{
    
}

int HttpTest::attachFd(SOCKET_FD fd)
{
    http_.setWriteCallback([this] (int err) { onSend(err); });
    http_.setErrorCallback([this] (int err) { onClose(err); });
    
    http_.setDataCallback([this] (uint8_t* data, uint32_t len) { onHttpData(data, len); });
    http_.setHeaderCompleteCallback([this] () { onHeaderComplete(); });
    http_.setRequestCompleteCallback([this] () { onRequestComplete(); });
    http_.setResponseCompleteCallback([this] () { onResponseComplete(); });
    
    return http_.attachFd(fd);
}

int HttpTest::close()
{
    return http_.close();
}

void HttpTest::onSend(int err)
{
    
}

void HttpTest::onClose(int err)
{
    printf("HttpTest::onClose, err=%d\n", err);
    server_->removeObject(conn_id_);
}

void HttpTest::onHttpData(uint8_t* data, uint32_t len)
{
    printf("HttpTest::onHttpData, len=%u\n", len);
}

void HttpTest::onHeaderComplete()
{
    printf("HttpTest::onHeaderComplete\n");
}

void HttpTest::onRequestComplete()
{
    printf("HttpTest::onRequestComplete\n");
    http_.addHeader("Content-Length", (uint32_t)0);
    http_.sendResponse(200, "OK");
}

void HttpTest::onResponseComplete()
{
    printf("HttpTest::onResponseComplete\n");
}
