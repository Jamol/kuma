#include "WsHandler.h"


using namespace kmsvr;
using namespace kuma;

WsHandler::WsHandler(const RunLoop::Ptr &loop, const std::string &ver)
: loop_(loop.get())
, ws_(loop->getEventLoop().get(), ver.c_str())
{
    ws_.setOpenCallback([this] (KMError err) { onOpen(err); });
    ws_.setWriteCallback([this] (KMError err) { onSend(err); });
    ws_.setErrorCallback([this] (KMError err) { onClose(err); });
    ws_.setDataCallback([this] (KMBuffer &buf, bool is_text, bool is_fin) {
        onData(buf, is_text, is_fin);
    });
}

KMError WsHandler::attachFd(SOCKET_FD fd, uint32_t ssl_flags, const KMBuffer *init_buf)
{
    ws_.setSslFlags(ssl_flags);
    return ws_.attachFd(fd, init_buf, [this] (KMError err) { return onHandshake(err); });
}

KMError WsHandler::attachSocket(TcpSocket&& tcp, HttpParser&& parser, const KMBuffer *init_buf)
{
    return ws_.attachSocket(std::move(tcp), std::move(parser), init_buf, [this] (KMError err) {
        return onHandshake(err);
    });
}

KMError WsHandler::attachStream(uint32_t stream_id, H2Connection* conn)
{
    return ws_.attachStream(stream_id, conn, [this] (KMError err) {
        return onHandshake(err);
    });
}

void WsHandler::close()
{
    ws_.close();
}

bool WsHandler::onHandshake(KMError err)
{
    printf("WsHandler::onHandshake, err=%d\n", (int)err);
    printf("WsHandler::onHandshake, path=%s, origin=%s\n", ws_.getPath(), ws_.getOrigin());
    ws_.forEachHeader([](const char *name, const char *value){
        printf("WsHandler::onHandshake, header, %s: %s\n", name, value);
        return true;
    });
    return true;
}

void WsHandler::onOpen(KMError err)
{
    printf("WsHandler::onOpen, err=%d\n", (int)err);
}

void WsHandler::onSend(KMError err)
{
    //sendTestData();
}

void WsHandler::onClose(KMError err)
{
    printf("WsHandler::onClose, err=%d\n", (int)err);
    ws_.close();
    loop_->removeObject(getObjectId());
}

void WsHandler::onData(KMBuffer &buf, bool is_text, bool is_fin)
{
    //printf("WsHandler::onData, len=%u\n", uint32_t(buf.chainLength()));
    int ret = ws_.send(buf, is_text, is_fin);
    if(ret < 0) {
        ws_.close();
        loop_->removeObject(getObjectId());
    }// else should buffer remain data if ret < len
}

void WsHandler::sendTestData()
{
    const size_t kBufferSize = 128*1024;
    uint8_t buf[kBufferSize] = {0};
    memset(buf, 'a', sizeof(buf));
    
    KMBuffer buf1(buf, kBufferSize/4, kBufferSize/4);
    KMBuffer buf2(buf + kBufferSize/4, kBufferSize/4, kBufferSize/4);
    KMBuffer buf3(buf + kBufferSize*2/4, kBufferSize/4, kBufferSize/4);
    KMBuffer buf4(buf + kBufferSize*3/4, kBufferSize/4, kBufferSize/4);
    buf1.append(&buf2);
    buf1.append(&buf3);
    buf1.append(&buf4);
    
    while (true) {
        int ret = ws_.send(buf1, false);
        if (ret < 0) {
            break;
        } else if ((size_t)ret < sizeof(buf)) {
            // should buffer remain data if ret < sizeof(buf)
            //printf("WsTest::sendTestData, break, ret=%d\n", ret);
            break;
        }
    }
}
