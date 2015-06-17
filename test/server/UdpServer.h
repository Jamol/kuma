#ifndef __UdpServer_H__
#define __UdpServer_H__

#include "kmapi.h"
#include "util/util.h"

using namespace kuma;

class UdpServer
{
public:
    UdpServer(EventLoop* loop);
    
    int bind(const char* host, uint16_t port);
    int close();
    
    void onReceive(int err);
    void onClose(int err);
    
private:
    EventLoop*  loop_;
    UdpSocket   udp_;
};

#endif
