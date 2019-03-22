#ifndef __WsTest_H__
#define __WsTest_H__

#include "kmapi.h"
#include "TestLoop.h"

#include <map>

using namespace kuma;

class WsTest : public TestObject
{
public:
    WsTest(ObjectManager* obj_mgr, long conn_id, const std::string &ver);

    KMError attachFd(SOCKET_FD fd, uint32_t ssl_flags, const KMBuffer *init_buf);
    KMError attachSocket(TcpSocket&& tcp, HttpParser&& parser, const KMBuffer *init_buf);
    KMError attachStream(uint32_t stream_id, H2Connection* conn);
    int close();
    
    bool onHandshake(KMError err);
    void onOpen(KMError err);
    void onSend(KMError err);
    void onClose(KMError err);
    
    void onData(KMBuffer &buf, bool is_text, bool is_fin);
    
private:
    void cleanup();
    void sendTestData();
    
private:
    ObjectManager*  obj_mgr_;
    WebSocket       ws_;
    long            conn_id_;
};

#endif
