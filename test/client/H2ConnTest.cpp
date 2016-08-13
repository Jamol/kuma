
#include "H2ConnTest.h"

H2ConnTest::H2ConnTest(TestLoop* loop, long conn_id)
: loop_(loop)
, conn_(loop->getEventLoop())
, conn_id_(conn_id)
{
    
}

void H2ConnTest::connect(const std::string& host, uint16_t port)
{
    conn_.setSslFlags(SSL_ENABLE);
    conn_.connect(host.c_str(), port, [this] (KMError err) { onConnect(err); });
}

int H2ConnTest::close()
{
    conn_.close();
    return 0;
}

void H2ConnTest::onConnect(KMError err)
{
    
}

