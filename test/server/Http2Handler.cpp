
#include "Http2Handler.h"
#include "HttpHandler.h"
#include "WsHandler.h"
#include "RunLoopPool.h"

#ifdef KUMA_OS_WIN
#define strcasecmp _stricmp
#endif

using namespace kmsvr;
using namespace kuma;

Http2Handler::Http2Handler(const RunLoop::Ptr &loop, RunLoopPool *pool)
: loop_(loop.get())
, pool_(pool)
, conn_(loop->getEventLoop().get())
{
    
}

Http2Handler::~Http2Handler()
{
    
}

KMError Http2Handler::attachSocket(TcpSocket&& tcp, HttpParser&& parser, const KMBuffer *init_buf)
{
    conn_.setAcceptCallback([this] (uint32_t streamId, const char *method, const char *path, const char *host,const char *protocol) {
        return onAccept(streamId, method, path, host, protocol);
    });
    conn_.setErrorCallback([this] (int err) {
        return onError(err);
    });
    return conn_.attachSocket(std::move(tcp), std::move(parser), init_buf);
}

void Http2Handler::close()
{
    conn_.close();
}

bool Http2Handler::onAccept(uint32_t stream_id, const char *method, const char *path, const char *host, const char *protocol)
{
    printf("Http2Handler::onAccept, streamId=%u, method=%s, proto=%s\n", stream_id, method, protocol);
    if (strcasecmp(method, "CONNECT") == 0 && strcasecmp(protocol, "websocket") == 0) {
        auto loop = pool_->getRunLoop();
        auto ws = std::make_shared<WsHandler>(loop, "HTTP/2.0");
        loop->addObject(ws->getObjectId(), ws);
        ws->attachStream(stream_id, &conn_);
    } else {
        auto loop = pool_->getRunLoop();
        auto http = std::make_shared<HttpHandler>(loop, "HTTP/2.0");
        loop->addObject(http->getObjectId(), http);
        http->attachStream(stream_id, &conn_);
    }
    return true;
}

void Http2Handler::onError(int err)
{
    conn_.close();
    loop_->removeObject(getObjectId());
}
