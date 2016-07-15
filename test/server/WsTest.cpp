#include "WsTest.h"
#include "TestLoop.h"

WsTest::WsTest(EventLoop* loop, long conn_id, TestLoop* server)
: loop_(loop)
, ws_(loop_)
, server_(server)
, conn_id_(conn_id)
{
    
}

int WsTest::attachFd(SOCKET_FD fd, uint32_t ssl_flags)
{
    ws_.setWriteCallback([this] (int err) { onSend(err); });
    ws_.setErrorCallback([this] (int err) { onClose(err); });
    ws_.setDataCallback([this] (uint8_t* data, size_t len) { onData(data, len); });
    ws_.setSslFlags(ssl_flags);
    return ws_.attachFd(fd);
}

int WsTest::attachSocket(TcpSocket&& tcp, HttpParser&& parser)
{
    ws_.setWriteCallback([this] (int err) { onSend(err); });
    ws_.setErrorCallback([this] (int err) { onClose(err); });
    ws_.setDataCallback([this] (uint8_t* data, size_t len) { onData(data, len); });
    
    return ws_.attachSocket(std::move(tcp), std::move(parser));
}

int WsTest::close()
{
    return ws_.close();
}

void WsTest::onSend(int err)
{
    //sendTestData();
}

void WsTest::onClose(int err)
{
    printf("WsTest::onClose, err=%d\n", err);
    server_->removeObject(conn_id_);
}

void WsTest::onData(uint8_t* data, size_t len)
{
    //printf("WsTest::onData, len=%u\n", len);
    int ret = ws_.send(data, len);
    if(ret < 0) {
        ws_.close();
        server_->removeObject(conn_id_);
    }
}

void WsTest::sendTestData()
{
    uint8_t buf[128*1024] = {0};
    memset(buf, 'a', sizeof(buf));
    while (true) {
        int ret = ws_.send(buf, sizeof(buf));
        if (ret < 0) {
            break;
        } else if (ret < sizeof(buf)) {
            //printf("WsTest::sendTestData, break, ret=%d\n", ret);
            break;
        }
    }
}
