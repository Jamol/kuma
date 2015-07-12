#ifndef __AutoHelper_H__
#define __AutoHelper_H__

#include "kmapi.h"
#include "util/util.h"
#include "TestLoop.h"

#include <map>

using namespace kuma;

class TestLoop;
class AutoHelper : public LoopObject
{
public:
    AutoHelper(EventLoop* loop, long conn_id, TestLoop* server);

    int attachFd(SOCKET_FD fd);
    int close();
    
    void onSend(int err);
    void onReceive(int err);
    void onClose(int err);
    
    void onHttpData(const char*, uint32_t);
    void onHttpEvent(HttpEvent ev);
    
private:
    void cleanup();
    
private:
    EventLoop*      loop_;
    TestLoop*       server_;
    long            conn_id_;
    
    TcpSocket       tcp_;
    HttpParser      http_parser_;
    bool*           destroy_flag_ptr_;
};

#endif
