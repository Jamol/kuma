#include "TcpTest.h"
#include "TcpServer.h"

TcpTest::TcpTest(TestLoop* loop, long conn_id)
: loop_(loop)
, tcp_(loop->eventLoop())
, conn_id_(conn_id)
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
    char buf[4096] = {0};
    do
    {
        int bytes_read = tcp_.receive((uint8_t*)buf, sizeof(buf));
        if(bytes_read < 0) {
            tcp_.close();
            loop_->removeObject(conn_id_);
            return ;
        } else if (0 == bytes_read){
            break;
        }
        int ret = tcp_.send((uint8_t*)buf, bytes_read);
        if(ret < 0) {
            tcp_.close();
            loop_->removeObject(conn_id_);
        }// else should buffer remain data if ret < bytes_read
    } while(true);
}

void TcpTest::onClose(KMError err)
{
    printf("TcpTest::onClose, err=%d\n", err);
    tcp_.close();
    loop_->removeObject(conn_id_);
}
