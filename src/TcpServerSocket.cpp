#include "kmconf.h"

#if defined(KUMA_OS_WIN)
# include <Ws2tcpip.h>
# include <windows.h>
# include <time.h>
#elif defined(KUMA_OS_LINUX)
# include <string.h>
# include <pthread.h>
# include <unistd.h>
# include <fcntl.h>
# include <sys/types.h>
# include <sys/stat.h>
# include <sys/time.h>
# include <sys/socket.h>
# include <netdb.h>
# include <arpa/inet.h>
# include <netinet/tcp.h>
# include <netinet/in.h>
#elif defined(KUMA_OS_MAC)
# include <string.h>
# include <pthread.h>
# include <unistd.h>
# include <sys/types.h>
# include <sys/socket.h>
# include <sys/ioctl.h>
# include <sys/fcntl.h>
# include <sys/time.h>
# include <sys/uio.h>
# include <netinet/tcp.h>
# include <netinet/in.h>
# include <arpa/inet.h>
# include <netdb.h>
# include <ifaddrs.h>
#else
# error "UNSUPPORTED OS"
#endif

#include <stdarg.h>
#include <errno.h>

#include "TcpServerSocket.h"
#include "EventLoop.h"
#include "util/util.h"
#include "util/kmtrace.h"

KUMA_NS_BEGIN

TcpServerSocket::TcpServerSocket(EventLoop* loop)
: fd_(INVALID_FD)
, loop_(loop)
, state_(ST_IDLE)
, registered_(false)
, flags_(0)
{
    
}

TcpServerSocket::~TcpServerSocket()
{

}

const char* TcpServerSocket::getObjKey()
{
    return "TcpServerSocket";
}

void TcpServerSocket::cleanup()
{
    if(INVALID_FD != fd_) {
        SOCKET_FD fd = fd_;
        fd_ = INVALID_FD;
        shutdown(fd, 0); // only stop receive
        if(registered_) {
            registered_ = false;
            loop_->unregisterFd(fd, true);
        } else {
            closeFd(fd);
        }
    }
}

int TcpServerSocket::startListen(const char* addr, uint16_t port)
{
    sockaddr_storage ss_addr = {0};
    struct addrinfo hints = {0};
    hints.ai_family = AF_UNSPEC;
    hints.ai_flags = AI_ADDRCONFIG; // will block 10 seconds in some case if not set AI_ADDRCONFIG
    if(km_set_sock_addr(addr, port, &hints, (struct sockaddr*)&ss_addr, sizeof(ss_addr)) != 0) {
        return KUMA_ERROR_INVALID_PARAM;
    }
    fd_ = ::socket(ss_addr.ss_family, SOCK_STREAM, 0);
    if(INVALID_FD == fd_) {
        KUMA_ERRXTRACE("startListen, socket failed, err="<<getLastError());
        return KUMA_ERROR_FAILED;
    }
    int ret = ::bind(fd_, (struct sockaddr*)&ss_addr, sizeof(ss_addr));
    if(ret < 0) {
        KUMA_ERRXTRACE("startListen, bind failed, err="<<getLastError());
        return KUMA_ERROR_FAILED;
    }
    return KUMA_ERROR_NOERR;
}

void TcpServerSocket::setSocketOption()
{
    if(INVALID_FD == fd_) {
        return ;
    }
    
#ifdef KUMA_OS_LINUX
    fcntl(fd_, F_SETFD, FD_CLOEXEC);
#endif
    
    // nonblock
#ifdef KUMA_OS_WIN
    int mode = 1;
    ::ioctlsocket(fd_,FIONBIO,(ULONG*)&mode);
#else
    int flag = fcntl(fd_, F_GETFL, 0);
    fcntl(fd_, F_SETFL, flag | O_NONBLOCK | O_ASYNC);
#endif
    
    int opt_val = 1;
    setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR, (char*)&opt_val, sizeof(opt_val));
}

int TcpServerSocket::close()
{
    KUMA_INFOXTRACE("close, state"<<getState());
    cleanup();
    setState(ST_CLOSED);
    return KUMA_ERROR_NOERR;
}

void TcpServerSocket::onAccept()
{
    
}

void TcpServerSocket::onClose(int err)
{
    
}

void TcpServerSocket::ioReady(uint32_t events)
{
    if(events & KUMA_EV_ERROR) {
        KUMA_ERRXTRACE("ioReady, EPOLLERR or EPOLLHUP, events="<<events
                       <<", state="<<getState());
        onClose(KUMA_ERROR_POLLERR);
    } else {
        
    }
}

KUMA_NS_END
