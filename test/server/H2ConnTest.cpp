
#include "H2ConnTest.h"
#include "HttpTest.h"
#include "WsTest.h"

#ifdef KUMA_OS_WIN
#define strcasecmp _stricmp
#endif

H2ConnTest::H2ConnTest(TestLoop* loop, long conn_id)
: loop_(loop)
, conn_(loop->eventLoop())
, conn_id_(conn_id)
{
    
}

H2ConnTest::~H2ConnTest()
{
    cleanup();
}

void H2ConnTest::cleanup()
{
    std::lock_guard<std::mutex> lg(h2_mutex_);
    for (auto &kv : obj_map_) {
        kv.second->close();
        delete kv.second;
    }
    obj_map_.clear();
}

KMError H2ConnTest::attachSocket(TcpSocket&& tcp, HttpParser&& parser, const KMBuffer *init_buf)
{
    conn_.setAcceptCallback([this] (uint32_t streamId, const char *method, const char *path, const char *host,const char *protocol) {
        return onAccept(streamId, method, path, host, protocol);
    });
    conn_.setErrorCallback([this] (int err) {
        return onError(err);
    });
    return conn_.attachSocket(std::move(tcp), std::move(parser), init_buf);
}

int H2ConnTest::close()
{
    cleanup();
    conn_.close();
    return 0;
}

bool H2ConnTest::onAccept(uint32_t stream_id, const char *method, const char *path, const char *host, const char *protocol)
{
    printf("H2ConnTest::onAccept, streamId=%u, method=%s, proto=%s\n", stream_id, method, protocol);
    if (strcasecmp(method, "CONNECT") == 0 && strcasecmp(protocol, "websocket") == 0) {
        WsTest* ws = new WsTest(this, long(stream_id), "HTTP/2.0");
        addObject(stream_id, ws);
        ws->attachStream(stream_id, &conn_);
    } else {
        HttpTest* h2 = new HttpTest(this, long(stream_id), "HTTP/2.0");
        addObject(stream_id, h2);
        h2->attachStream(stream_id, &conn_);
    }
    return true;
}

void H2ConnTest::onError(int err)
{
    cleanup();
    conn_.close();
    loop_->removeObject(conn_id_);
}

void H2ConnTest::addObject(long conn_id, TestObject* obj)
{
    std::lock_guard<std::mutex> lg(h2_mutex_);
    obj_map_.emplace(conn_id, obj);
}

void H2ConnTest::removeObject(long conn_id)
{
    printf("H2ConnTest::removeObject, conn_id=%ld\n", conn_id);
    std::lock_guard<std::mutex> lg(h2_mutex_);
    auto it = obj_map_.find(conn_id);
    if(it != obj_map_.end()) {
        delete it->second;
        obj_map_.erase(it);
    }
}
