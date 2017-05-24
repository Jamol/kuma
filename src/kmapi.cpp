/* Copyright (c) 2014, Fengping Bao <jamol@live.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include "EventLoopImpl.h"
#include "TcpSocketImpl.h"
#include "UdpSocketImpl.h"
#include "TcpListenerImpl.h"
#include "TimerManager.h"
#include "http/HttpParserImpl.h"
#include "http/Http1xRequest.h"
#include "http/Http1xResponse.h"
#include "http/HttpResponseImpl.h"
#include "ws/WebSocketImpl.h"
#include "http/v2/H2ConnectionImpl.h"
#include "http/v2/Http2Request.h"
#include "http/v2/Http2Response.h"

#ifdef KUMA_HAS_OPENSSL
#include "ssl/OpenSslLib.h"
#endif
#include "DnsResolver.h"

#include "kmapi.h"

KUMA_NS_BEGIN

// don't want to expose shared_ptr to interface
template <typename Impl>
struct ImplHelper {
    using ImplPtr = std::shared_ptr<Impl>;
    Impl impl;
    ImplPtr ptr;

    template <typename... Args>
    ImplHelper(Args&... args)
        : impl(args...)
    {

    }

    template <typename... Args>
    ImplHelper(Args&&... args)
        : impl(std::forward<Args...>(args)...)
    {

    }
    
    static ImplPtr toSharedPtr(Impl *pimpl)
    {
        if (pimpl) {
            auto h = reinterpret_cast<ImplHelper<Impl>*>(pimpl);
            return h->ptr;
        }
        return ImplPtr();
    }
};
using EventLoopHelper = ImplHelper<EventLoop::Impl>;

EventLoop::EventLoop(PollType poll_type)
{
    auto h = new EventLoopHelper(poll_type);
    pimpl_ = reinterpret_cast<Impl*>(h);
    h->ptr.reset(pimpl_, [](Impl* p){
        auto h = reinterpret_cast<EventLoopHelper*>(p);
        delete h;
    });
}

EventLoop::~EventLoop()
{
    auto h = reinterpret_cast<EventLoopHelper*>(pimpl_);
    h->ptr.reset();
}

bool EventLoop::init()
{
    return pimpl_->init();
}

EventLoop::Token EventLoop::createToken()
{
    Token t;
    t.pimpl()->eventLoop(EventLoopHelper::toSharedPtr(pimpl()));
    return std::move(t);
}

PollType EventLoop::getPollType() const
{
    return pimpl_->getPollType();
}

bool EventLoop::isPollLT() const
{
    return  pimpl_->isPollLT();
}

KMError EventLoop::registerFd(SOCKET_FD fd, uint32_t events, IOCallback cb)
{
    return pimpl_->registerFd(fd, events, std::move(cb));
}

KMError EventLoop::updateFd(SOCKET_FD fd, uint32_t events)
{
    return pimpl_->updateFd(fd, events);
}

KMError EventLoop::unregisterFd(SOCKET_FD fd, bool close_fd)
{
    return pimpl_->unregisterFd(fd, close_fd);
}

void EventLoop::loopOnce(uint32_t max_wait_ms)
{
    pimpl_->loopOnce(max_wait_ms);
}

void EventLoop::loop(uint32_t max_wait_ms)
{
    pimpl_->loop(max_wait_ms);
}

void EventLoop::stop()
{
    pimpl_->stop();
}

EventLoop::Impl* EventLoop::pimpl()
{
    return pimpl_;
}

KMError EventLoop::sync(Task task)
{
    return pimpl_->sync(std::move(task));
}

KMError EventLoop::async(Task task, Token *token)
{
    return pimpl_->async(std::move(task), token?token->pimpl():nullptr);
}

KMError EventLoop::queue(Task task, Token *token)
{
    return pimpl_->queue(std::move(task), token?token->pimpl():nullptr);
}

void EventLoop::cancel(Token *token)
{
    if (token) {
        pimpl_->removeTask(token->pimpl());
    }
}

EventLoop::Token::Token()
: pimpl_(new EventLoopToken())
{
    
}

EventLoop::Token::Token(Token &&other)
: pimpl_(other.pimpl_)
{
    other.pimpl_ = nullptr;
}

EventLoop::Token::~Token()
{
    delete pimpl_;
}

EventLoop::Token& EventLoop::Token::operator=(Token &&other)
{
    if (this != &other) {
        pimpl_ = other.pimpl_;
        other.pimpl_ = nullptr;
    }
    return *this;
}

void EventLoop::Token::reset()
{
    if (pimpl_) {
        pimpl_->reset();
    }
}

EventLoopToken* EventLoop::Token::pimpl()
{
    return pimpl_;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////
TcpSocket::TcpSocket(EventLoop* loop)
: pimpl_(new Impl(EventLoopHelper::toSharedPtr(loop->pimpl())))
{

}

TcpSocket::~TcpSocket()
{
    delete pimpl_;
}

KMError TcpSocket::setSslFlags(uint32_t ssl_flags)
{
    return pimpl_->setSslFlags(ssl_flags);
}

uint32_t TcpSocket::getSslFlags() const
{
    return pimpl_->getSslFlags();
}

bool TcpSocket::sslEnabled() const
{
    return pimpl_->sslEnabled();
}

KMError TcpSocket::setSslServerName(const char *server_name)
{
#ifdef KUMA_HAS_OPENSSL
    return pimpl_->setSslServerName(server_name);
#else
    return KMError::UNSUPPORT;
#endif
}

KMError TcpSocket::bind(const char* bind_host, uint16_t bind_port)
{
    return pimpl_->bind(bind_host, bind_port);
}

KMError TcpSocket::connect(const char* host, uint16_t port, EventCallback cb, uint32_t timeout)
{
    return pimpl_->connect(host, port, std::move(cb), timeout);
}

KMError TcpSocket::attachFd(SOCKET_FD fd)
{
    return pimpl_->attachFd(fd);
}

KMError TcpSocket::detachFd(SOCKET_FD &fd)
{
    return pimpl_->detachFd(fd);
}

KMError TcpSocket::startSslHandshake(SslRole ssl_role)
{
#ifdef KUMA_HAS_OPENSSL
    return pimpl_->startSslHandshake(ssl_role);
#else
    return KMError::UNSUPPORT;
#endif
}

KMError TcpSocket::getAlpnSelected(char *buf, size_t len)
{
#ifdef KUMA_HAS_OPENSSL
    std::string proto;
    auto ret = pimpl_->getAlpnSelected(proto);
    if (ret == KMError::NOERR) {
        if (proto.size() >= len) {
            return KMError::BUFFER_TOO_SMALL;
        }
        memcpy(buf, proto.c_str(), proto.size());
        buf[proto.size()] = '\0';
    }
    return ret;
#else
    return KMError::UNSUPPORT;
#endif
}

int TcpSocket::send(const void* data, size_t length)
{
    return pimpl_->send(data, length);
}

int TcpSocket::send(iovec* iovs, int count)
{
    return pimpl_->send(iovs, count);
}

int TcpSocket::receive(void* data, size_t length)
{
    return pimpl_->receive(data, length);
}

KMError TcpSocket::close()
{
    return pimpl_->close();
}

KMError TcpSocket::pause()
{
    return pimpl_->pause();
}

KMError TcpSocket::resume()
{
    return pimpl_->resume();
}

void TcpSocket::setReadCallback(EventCallback cb)
{
    pimpl_->setReadCallback(std::move(cb));
}

void TcpSocket::setWriteCallback(EventCallback cb)
{
    pimpl_->setWriteCallback(std::move(cb));
}

void TcpSocket::setErrorCallback(EventCallback cb)
{
    pimpl_->setErrorCallback(std::move(cb));
}

SOCKET_FD TcpSocket::getFd() const
{
    return pimpl_->getFd();
}

TcpSocket::Impl* TcpSocket::pimpl()
{
    return pimpl_;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////
TcpListener::TcpListener(EventLoop* loop)
: pimpl_(new Impl(EventLoopHelper::toSharedPtr(loop->pimpl())))
{

}
TcpListener::~TcpListener()
{
    delete pimpl_;
}

KMError TcpListener::startListen(const char* host, uint16_t port)
{
    return pimpl_->startListen(host, port);
}

KMError TcpListener::stopListen(const char* host, uint16_t port)
{
    return pimpl_->stopListen(host, port);
}

KMError TcpListener::close()
{
    return pimpl_->close();
}

void TcpListener::setAcceptCallback(AcceptCallback cb)
{
    pimpl_->setAcceptCallback(std::move(cb));
}

void TcpListener::setErrorCallback(ErrorCallback cb)
{
    pimpl_->setErrorCallback(std::move(cb));
}

TcpListener::Impl* TcpListener::pimpl()
{
    return pimpl_;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////
UdpSocket::UdpSocket(EventLoop* loop)
: pimpl_(new Impl(EventLoopHelper::toSharedPtr(loop->pimpl())))
{
    
}

UdpSocket::~UdpSocket()
{
    delete pimpl_;
}

KMError UdpSocket::bind(const char* bind_host, uint16_t bind_port, uint32_t udp_flags)
{
    return pimpl_->bind(bind_host, bind_port, udp_flags);
}

int UdpSocket::send(const void* data, size_t length, const char* host, uint16_t port)
{
    return pimpl_->send(data, length, host, port);
}

int UdpSocket::send(iovec* iovs, int count, const char* host, uint16_t port)
{
    return pimpl_->send(iovs, count, host, port);
}

int UdpSocket::receive(void* data, size_t length, char* ip, size_t ip_len, uint16_t& port)
{
    return pimpl_->receive(data, length, ip, ip_len, port);
}

KMError UdpSocket::close()
{
    return pimpl_->close();
}

KMError UdpSocket::mcastJoin(const char* mcast_addr, uint16_t mcast_port)
{
    return pimpl_->mcastJoin(mcast_addr, mcast_port);
}

KMError UdpSocket::mcastLeave(const char* mcast_addr, uint16_t mcast_port)
{
    return pimpl_->mcastLeave(mcast_addr, mcast_port);
}

void UdpSocket::setReadCallback(EventCallback cb)
{
    pimpl_->setReadCallback(std::move(cb));
}

void UdpSocket::setErrorCallback(EventCallback cb)
{
    pimpl_->setErrorCallback(std::move(cb));
}

UdpSocket::Impl* UdpSocket::pimpl()
{
    return pimpl_;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////

Timer::Timer(EventLoop* loop)
: pimpl_(new Impl(loop->pimpl()->getTimerMgr()))
{
    
}

Timer::~Timer()
{
    delete pimpl_;
}

bool Timer::schedule(uint32_t delay_ms, TimerCallback cb, TimerMode mode)
{
    return pimpl_->schedule(delay_ms, std::move(cb), mode);
}

void Timer::cancel()
{
    pimpl_->cancel();
}

Timer::Impl* Timer::pimpl()
{
    return pimpl_;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////
HttpParser::HttpParser()
: pimpl_(new Impl())
{
    
}

HttpParser::~HttpParser()
{
    delete pimpl_;
}

// return bytes parsed
int HttpParser::parse(char* data, size_t len)
{
    return pimpl_->parse(data, len);
}

void HttpParser::pause()
{
    pimpl_->pause();
}

void HttpParser::resume()
{
    pimpl_->resume();
}

bool HttpParser::setEOF()
{
    return pimpl_->setEOF();
}

void HttpParser::reset()
{
    pimpl_->reset();
}

bool HttpParser::isRequest() const
{
    return pimpl_->isRequest();
}

bool HttpParser::headerComplete() const
{
    return pimpl_->headerComplete();
}

bool HttpParser::complete() const
{
    return pimpl_->complete();
}

bool HttpParser::error() const
{
    return pimpl_->error();
}

bool HttpParser::paused() const
{
    return pimpl_->paused();
}

bool HttpParser::isUpgradeTo(const char* proto) const
{
    return pimpl_->isUpgradeTo(proto);
}

int HttpParser::getStatusCode() const
{
    return pimpl_->getStatusCode();
}

const char* HttpParser::getUrl() const
{
    return pimpl_->getLocation().c_str();
}

const char* HttpParser::getUrlPath() const
{
    return pimpl_->getUrlPath().c_str();
}

const char* HttpParser::getMethod() const
{
    return pimpl_->getMethod().c_str();
}

const char* HttpParser::getVersion() const
{
    return pimpl_->getVersion().c_str();
}

const char* HttpParser::getParamValue(const char* name) const
{
    return pimpl_->getParamValue(name).c_str();
}

const char* HttpParser::getHeaderValue(const char* name) const
{
    return pimpl_->getHeaderValue(name).c_str();
}

void HttpParser::forEachParam(EnumrateCallback cb)
{
    pimpl_->forEachParam([&cb](const std::string& name, const std::string& value) {
        cb(name.c_str(), value.c_str());
    });
}

void HttpParser::forEachHeader(EnumrateCallback cb)
{
    pimpl_->forEachHeader([&cb](const std::string& name, const std::string& value) {
        cb(name.c_str(), value.c_str());
    });
}

void HttpParser::setDataCallback(DataCallback cb)
{
    pimpl_->setDataCallback(std::move(cb));
}

void HttpParser::setEventCallback(EventCallback cb)
{
    pimpl_->setEventCallback(std::move(cb));
}

HttpParser::Impl* HttpParser::pimpl()
{
    return pimpl_;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////
HttpRequest::HttpRequest(EventLoop* loop, const char* ver)
{
    auto loop_ptr = EventLoopHelper::toSharedPtr(loop->pimpl());
    if (is_equal(ver, VersionHTTP2_0)) {
        pimpl_ = new Http2Request(loop_ptr, ver);
    } else {
        pimpl_ = new Http1xRequest(loop_ptr, ver);
    }
}

HttpRequest::~HttpRequest()
{
    delete pimpl_;
}

KMError HttpRequest::setSslFlags(uint32_t ssl_flags)
{
    return pimpl_->setSslFlags(ssl_flags);
}

void HttpRequest::addHeader(const char* name, const char* value)
{
    pimpl_->addHeader(name, value);
}

void HttpRequest::addHeader(const char* name, uint32_t value)
{
    pimpl_->addHeader(name, value);
}

KMError HttpRequest::sendRequest(const char* method, const char* url)
{
    return pimpl_->sendRequest(method, url);
}

int HttpRequest::sendData(const void* data, size_t len)
{
    return pimpl_->sendData(data, len);
}

void HttpRequest::reset()
{
    pimpl_->reset();
}

KMError HttpRequest::close()
{
    return pimpl_->close();
}

int HttpRequest::getStatusCode() const
{
    return pimpl_->getStatusCode();
}

const char* HttpRequest::getVersion() const
{
    return pimpl_->getVersion().c_str();
}

const char* HttpRequest::getHeaderValue(const char* name) const
{
    return pimpl_->getHeaderValue(name).c_str();
}

void HttpRequest::forEachHeader(HttpParser::EnumrateCallback cb)
{
    pimpl_->forEachHeader([&cb] (const std::string& name, const std::string& value) {
        cb(name.c_str(), value.c_str());
    });
}

void HttpRequest::setDataCallback(DataCallback cb)
{
    pimpl_->setDataCallback(std::move(cb));
}

void HttpRequest::setWriteCallback(EventCallback cb)
{
    pimpl_->setWriteCallback(std::move(cb));
}

void HttpRequest::setErrorCallback(EventCallback cb)
{
    pimpl_->setErrorCallback(std::move(cb));
}

void HttpRequest::setHeaderCompleteCallback(HttpEventCallback cb)
{
    pimpl_->setHeaderCompleteCallback(std::move(cb));
}

void HttpRequest::setResponseCompleteCallback(HttpEventCallback cb)
{
    pimpl_->setResponseCompleteCallback(std::move(cb));
}

HttpRequest::Impl* HttpRequest::pimpl()
{
    return pimpl_;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////

HttpResponse::HttpResponse(EventLoop* loop, const char* ver)
{
    auto loop_ptr = EventLoopHelper::toSharedPtr(loop->pimpl());
    if (is_equal(ver, VersionHTTP2_0)) {
        pimpl_ = new Http2Response(loop_ptr, ver);
    } else {
        pimpl_ = new Http1xResponse(loop_ptr, ver);
    }
}

HttpResponse::~HttpResponse()
{
    delete pimpl_;
}

KMError HttpResponse::setSslFlags(uint32_t ssl_flags)
{
    return pimpl_->setSslFlags(ssl_flags);
}

KMError HttpResponse::attachFd(SOCKET_FD fd, const void* init_data, size_t init_len)
{
    return pimpl_->attachFd(fd, init_data, init_len);
}

KMError HttpResponse::attachSocket(TcpSocket&& tcp, HttpParser&& parser, const void* init_data, size_t init_len)
{
    return pimpl_->attachSocket(std::move(*tcp.pimpl()), std::move(*parser.pimpl()), init_data, init_len);
}

void HttpResponse::addHeader(const char* name, const char* value)
{
    return pimpl_->addHeader(name, value);
}

void HttpResponse::addHeader(const char* name, uint32_t value)
{
    return pimpl_->addHeader(name, value);
}

KMError HttpResponse::sendResponse(int status_code, const char* desc)
{
    return pimpl_->sendResponse(status_code, desc);
}

int HttpResponse::sendData(const void* data, size_t len)
{
    return pimpl_->sendData(data, len);
}

void HttpResponse::reset()
{
    pimpl_->reset();
}

KMError HttpResponse::close()
{
    return pimpl_->close();
}

const char* HttpResponse::getMethod() const
{
    return pimpl_->getMethod().c_str();
}

const char* HttpResponse::getPath() const
{
    return pimpl_->getPath().c_str();
}

const char* HttpResponse::getVersion() const
{
    return pimpl_->getVersion().c_str();
}

const char* HttpResponse::getParamValue(const char* name) const
{
    return pimpl_->getParamValue(name).c_str();
}

const char* HttpResponse::getHeaderValue(const char* name) const
{
    return pimpl_->getHeaderValue(name).c_str();
}

void HttpResponse::forEachHeader(HttpParser::EnumrateCallback cb)
{
    pimpl_->forEachHeader([&cb] (const std::string& name, const std::string& value) {
        cb(name.c_str(), value.c_str());
    });
}

void HttpResponse::setDataCallback(DataCallback cb)
{
    pimpl_->setDataCallback(std::move(cb));
}

void HttpResponse::setWriteCallback(EventCallback cb)
{
    pimpl_->setWriteCallback(std::move(cb));
}

void HttpResponse::setErrorCallback(EventCallback cb)
{
    pimpl_->setErrorCallback(std::move(cb));
}

void HttpResponse::setHeaderCompleteCallback(HttpEventCallback cb)
{
    pimpl_->setHeaderCompleteCallback(std::move(cb));
}

void HttpResponse::setRequestCompleteCallback(HttpEventCallback cb)
{
    pimpl_->setRequestCompleteCallback(std::move(cb));
}

void HttpResponse::setResponseCompleteCallback(HttpEventCallback cb)
{
    pimpl_->setResponseCompleteCallback(std::move(cb));
}

HttpResponse::Impl* HttpResponse::pimpl()
{
    return pimpl_;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////

WebSocket::WebSocket(EventLoop* loop)
: pimpl_(new Impl(EventLoopHelper::toSharedPtr(loop->pimpl())))
{
    
}

WebSocket::~WebSocket()
{
    delete pimpl_;
}

KMError WebSocket::setSslFlags(uint32_t ssl_flags)
{
    return pimpl_->setSslFlags(ssl_flags);
}

void WebSocket::setProtocol(const char* proto)
{
    pimpl_->setProtocol(proto);
}

const char* WebSocket::getProtocol() const
{
    return pimpl_->getProtocol().c_str();
}

void WebSocket::setOrigin(const char* origin)
{
    pimpl_->setOrigin(origin);
}

const char* WebSocket::getOrigin() const
{
    return pimpl_->getOrigin().c_str();
}

KMError WebSocket::connect(const char* ws_url, EventCallback cb)
{
    return pimpl_->connect(ws_url, std::move(cb));
}

KMError WebSocket::attachFd(SOCKET_FD fd, const void* init_data, size_t init_len)
{
    return pimpl_->attachFd(fd, init_data, init_len);
}

KMError WebSocket::attachSocket(TcpSocket&& tcp, HttpParser&& parser, const void* init_data, size_t init_len)
{
    return pimpl_->attachSocket(std::move(*tcp.pimpl()), std::move((*parser.pimpl())), init_data, init_len);
}

int WebSocket::send(const void* data, size_t len, bool is_text, bool fin)
{
    return pimpl_->send(data, len, is_text, fin);
}

KMError WebSocket::close()
{
    return pimpl_->close();
}

void WebSocket::setDataCallback(DataCallback cb)
{
    pimpl_->setDataCallback(std::move(cb));
}

void WebSocket::setWriteCallback(EventCallback cb)
{
    pimpl_->setWriteCallback(std::move(cb));
}

void WebSocket::setErrorCallback(EventCallback cb)
{
    pimpl_->setErrorCallback(std::move(cb));
}

WebSocket::Impl* WebSocket::pimpl()
{
    return pimpl_;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////
H2Connection::H2Connection(EventLoop* loop)
: pimpl_(new Impl(EventLoopHelper::toSharedPtr(loop->pimpl())))
{
    
}

H2Connection::~H2Connection()
{
    delete pimpl_;
}

KMError H2Connection::setSslFlags(uint32_t ssl_flags)
{
    return pimpl_->setSslFlags(ssl_flags);
}
/*
KMError H2Connection::connect(const char* host, uint16_t port, ConnectCallback cb)
{
    return pimpl_->connect(host, port, cb);
}
*/
KMError H2Connection::attachFd(SOCKET_FD fd, const void* init_data, size_t init_len)
{
    return pimpl_->attachFd(fd, init_data, init_len);
}

KMError H2Connection::attachSocket(TcpSocket &&tcp, HttpParser &&parser, const void* init_data, size_t init_len)
{
    return pimpl_->attachSocket(std::move(*tcp.pimpl()), std::move(*parser.pimpl()), init_data, init_len);
}

KMError H2Connection::attachStream(uint32_t streamId, HttpResponse* rsp)
{
    return pimpl_->attachStream(streamId, rsp->pimpl());
}

KMError H2Connection::close()
{
    return pimpl_->close();
}

void H2Connection::setAcceptCallback(AcceptCallback cb)
{
    pimpl_->setAcceptCallback(std::move(cb));
}

void H2Connection::setErrorCallback(ErrorCallback cb)
{
    pimpl_->setErrorCallback(std::move(cb));
}

H2Connection::Impl* H2Connection::pimpl()
{
    return pimpl_;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////
//

void init(const char* path)
{
#ifdef KUMA_HAS_OPENSSL
    std::string cfg_path(path?path:"");
    OpenSslLib::init(cfg_path);
#endif
}

void fini()
{
#ifdef KUMA_HAS_OPENSSL
    OpenSslLib::fini();
#endif
    DnsResolver::get().stop();
}

KUMA_NS_END
