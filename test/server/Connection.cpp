#include "Connection.h"
#include "TcpServer.h"

Connection::Connection(EventLoop* loop, long conn_id, TestLoop* server)
: loop_(loop)
, tcp_(loop_)
, server_(server)
, conn_id_(conn_id)
{
    
}

int Connection::attachFd(SOCKET_FD fd)
{
    tcp_.setReadCallback([this] (int err) { onReceive(err); });
    tcp_.setWriteCallback([this] (int err) { onSend(err); });
    tcp_.setErrorCallback([this] (int err) { onClose(err); });
    
    return tcp_.attachFd(fd);
}

int Connection::close()
{
    return tcp_.close();
}

void Connection::onSend(int err)
{
    
}

void Connection::onReceive(int err)
{
    char buf[4096] = {0};
    do
    {
        int bytes_read = tcp_.receive((uint8_t*)buf, sizeof(buf));
        if(bytes_read < 0) {
            tcp_.close();
            server_->removeObject(conn_id_);
            return ;
        } else if (0 == bytes_read){
            break;
        }
        int ret = tcp_.send((uint8_t*)buf, bytes_read);
        if(ret < 0) {
            tcp_.close();
            server_->removeObject(conn_id_);
        }
    } while(true);
}

void Connection::onClose(int err)
{
    printf("Connection::onClose, err=%d\n", err);
    server_->removeObject(conn_id_);
}
