#ifndef __AutoHelper_H__
#define __AutoHelper_H__

#include "kmapi.h"
#include "TestLoop.h"

#include <map>

using namespace kuma;

class ProtoDemuxer : public LoopObject
{
public:
    ProtoDemuxer(TestLoop* loop, long conn_id);
    ~ProtoDemuxer();

    int attachFd(SOCKET_FD fd, uint32_t ssl_flags);
    int close();
    
    void onSend(int err);
    void onReceive(int err);
    void onClose(int err);
    
    void onHttpData(const char*, size_t);
    void onHttpEvent(HttpEvent ev);
    
private:
    void cleanup();
    bool checkHttp2();
    void demuxHttp();
    
private:
    TestLoop*       loop_;
    long            conn_id_;
    
    TcpSocket       tcp_;
    HttpParser      http_parser_;
    bool*           destroy_flag_ptr_;
};

#endif
