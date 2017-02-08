#include "WsTest.h"
#include "TestLoop.h"

#include <string.h>

WsTest::WsTest(TestLoop* loop, long conn_id)
: loop_(loop)
, ws_(loop->getEventLoop())
, conn_id_(conn_id)
{
    ws_.setWriteCallback([this] (KMError err) { onSend(err); });
    ws_.setErrorCallback([this] (KMError err) { onClose(err); });
    ws_.setDataCallback([this] (void* data, size_t len, bool is_text, bool fin) {
        onData(data, len);
    });
}

KMError WsTest::attachFd(SOCKET_FD fd, uint32_t ssl_flags, void *init, size_t len)
{
    ws_.setSslFlags(ssl_flags);
    return ws_.attachFd(fd, init, len);
}

KMError WsTest::attachSocket(TcpSocket&& tcp, HttpParser&& parser, void *init, size_t len)
{
    return ws_.attachSocket(std::move(tcp), std::move(parser), init, len);
}

int WsTest::close()
{
    ws_.close();
    return 0;
}

void WsTest::onSend(KMError err)
{
    //sendTestData();
}

void WsTest::onClose(KMError err)
{
    printf("WsTest::onClose, err=%d\n", err);
    ws_.close();
    loop_->removeObject(conn_id_);
}

void WsTest::onData(void* data, size_t len)
{
    //printf("WsTest::onData, len=%u\n", len);
    int ret = ws_.send(data, len, false);
    if(ret < 0) {
        ws_.close();
        loop_->removeObject(conn_id_);
    }// else should buffer remain data if ret < len
}

void WsTest::sendTestData()
{
    uint8_t buf[128*1024] = {0};
    memset(buf, 'a', sizeof(buf));
    while (true) {
        int ret = ws_.send(buf, sizeof(buf), false);
        if (ret < 0) {
            break;
        } else if (ret < sizeof(buf)) {
            // should buffer remain data if ret < sizeof(buf)
            //printf("WsTest::sendTestData, break, ret=%d\n", ret);
            break;
        }
    }
}
