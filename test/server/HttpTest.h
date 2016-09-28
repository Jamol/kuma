#ifndef __HttpTest_H__
#define __HttpTest_H__

#include "kmapi.h"
#include "TestLoop.h"

#include <map>

using namespace kuma;

class HttpTest : public TestObject
{
public:
    HttpTest(ObjectManager* obj_mgr, long conn_id, const char* ver);

    KMError attachFd(SOCKET_FD fd, uint32_t ssl_flags);
    KMError attachSocket(TcpSocket&& tcp, HttpParser&& parser);
    KMError attachStream(H2Connection* conn, uint32_t streamId);
    int close();
    
    void onSend(KMError err);
    void onClose(KMError err);
    
    void onHttpData(uint8_t*, size_t);
    void onHeaderComplete();
    void onRequestComplete();
    void onResponseComplete();
    
private:
    void setupCallbacks();
    void cleanup();
    void sendTestData();
    
private:
    ObjectManager*  obj_mgr_;
    HttpResponse    http_;
    long            conn_id_;
    bool            is_options_;
};

#endif
