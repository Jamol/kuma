
#include "H2ConnTest.h"

H2ConnTest::H2ConnTest(TestLoop* loop, long conn_id)
: loop_(loop)
, conn_(loop->getEventLoop())
, conn_id_(conn_id)
{
    
}

int H2ConnTest::attachSocket(TcpSocket&& tcp, HttpParser&& parser)
{
    return conn_.attachSocket(std::move(tcp), std::move(parser));
}

int H2ConnTest::close()
{
    conn_.close();
    return 0;
}
