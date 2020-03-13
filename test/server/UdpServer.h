#ifndef __UdpServer_H__
#define __UdpServer_H__

#include "kmapi.h"

#include <string>

using namespace kuma;

class UdpServer
{
public:
    UdpServer(EventLoop* loop);
    
    KMError bind(const std::string &host, uint16_t port);
    int close();
    
    void onReceive(KMError err);
    void onClose(KMError err);
    
private:
    EventLoop*  loop_;
    UdpSocket   udp_;
};

#endif
