
#include "H2ConnTest.h"

H2ConnTest::H2ConnTest(TestLoop* loop, long conn_id)
: loop_(loop)
, conn_(loop->getEventLoop())
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
    for (auto &kv : h2_map_) {
        kv.second->close();
        delete kv.second;
    }
    h2_map_.clear();
}

KMError H2ConnTest::attachSocket(TcpSocket&& tcp, HttpParser&& parser)
{
    conn_.setAcceptCallback([this] (uint32_t streamId) -> bool {
        return onAccept(streamId);
    });
    conn_.setErrorCallback([this] (int err) {
        return onError(err);
    });
    return conn_.attachSocket(std::move(tcp), std::move(parser));
}

int H2ConnTest::close()
{
    cleanup();
    conn_.close();
    return 0;
}

bool H2ConnTest::onAccept(uint32_t streamId)
{
    printf("H2ConnTest::onAccept, streamId=%u\n", streamId);
    HttpTest* h2 = new HttpTest(this, long(streamId), "HTTP/2.0");
    addObject(streamId, h2);
    h2->attachStream(&conn_, streamId);
    return true;
}

void H2ConnTest::onError(int err)
{
    cleanup();
    conn_.close();
}

void H2ConnTest::addObject(long conn_id, TestObject* obj)
{
    HttpTest* h2 = dynamic_cast<HttpTest*>(obj);
    if (!h2) {
        return;
    }
    std::lock_guard<std::mutex> lg(h2_mutex_);
    h2_map_.insert(std::make_pair(conn_id, h2));
}

void H2ConnTest::removeObject(long conn_id)
{
    printf("H2ConnTest::removeObject, conn_id=%ld\n", conn_id);
    std::lock_guard<std::mutex> lg(h2_mutex_);
    auto it = h2_map_.find(conn_id);
    if(it != h2_map_.end()) {
        delete it->second;
        h2_map_.erase(it);
    }
}
