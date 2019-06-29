#include "TcpTest.h"
#include "TcpServer.h"

//#define ECHO_TEST

TcpTest::TcpTest(TestLoop* loop, long conn_id)
: loop_(loop)
, tcp_(loop->eventLoop())
, conn_id_(conn_id)
, recv_reporter_("recv_bitrate")
{
    
}

KMError TcpTest::attachFd(SOCKET_FD fd)
{
    tcp_.setReadCallback([this] (KMError err) { onReceive(err); });
    tcp_.setWriteCallback([this] (KMError err) { onSend(err); });
    tcp_.setErrorCallback([this] (KMError err) { onClose(err); });
    
    return tcp_.attachFd(fd);
}

int TcpTest::close()
{
    tcp_.close();
    return 0;
}

void TcpTest::onSend(KMError err)
{
    
}

void TcpTest::onReceive(KMError err)
{
    size_t bytes_read = 0;
    char buf[4096] = {0};
    do
    {
        int ret = tcp_.receive((uint8_t*)buf, sizeof(buf));
        if(ret < 0) {
            tcp_.close();
            loop_->removeObject(conn_id_);
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

void TcpTest::onClose(KMError err)
{
    printf("TcpTest::onClose, err=%d\n", err);
    tcp_.close();
    loop_->removeObject(conn_id_);
}
