#include "TcpHandler.h"

//#define ECHO_TEST

using namespace kmsvr;
using namespace kuma;

TcpHandler::TcpHandler(const RunLoop::Ptr &loop)
: loop_(loop.get())
, tcp_(loop->getEventLoop().get())
, recv_reporter_("recv_bitrate")
{
    
}

KMError TcpHandler::attachFd(SOCKET_FD fd)
{
    tcp_.setReadCallback([this] (KMError err) { onReceive(err); });
    tcp_.setWriteCallback([this] (KMError err) { onSend(err); });
    tcp_.setErrorCallback([this] (KMError err) { onClose(err); });
    
    return tcp_.attachFd(fd);
}

void TcpHandler::close()
{
    tcp_.close();
}

void TcpHandler::onSend(KMError err)
{
    
}

void TcpHandler::onReceive(KMError err)
{
    size_t bytes_read = 0;
    char buf[4096] = {0};
    do
    {
        int ret = tcp_.receive((uint8_t*)buf, sizeof(buf));
        if(ret < 0) {
            tcp_.close();
            loop_->removeObject(getObjectId());
            return ;
        } else if (0 == ret){
            break;
        }
        bytes_read += ret;
#ifdef ECHO_TEST
        ret = tcp_.send((uint8_t*)buf, ret);
        if(ret < 0) {
            tcp_.close();
            loop_->removeObject(conn_id_);
        }// else should buffer remain data if ret < bytes_read
#endif
    } while(true);
    if (bytes_read > 0) {
        recv_reporter_.report(bytes_read * 8, steady_clock::now());
    }
}

void TcpHandler::onClose(KMError err)
{
    printf("TcpHandler::onClose, err=%d\n", (int)err);
    tcp_.close();
    loop_->removeObject(getObjectId());
}
