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
#include "libkev/src/TimerManager.h"
#include "http/HttpParserImpl.h"
#include "http/Http1xRequest.h"
#include "http/Http1xResponse.h"
#include "http/HttpResponseImpl.h"
#include "ws/WebSocketImpl.h"
#include "http/v2/H2ConnectionImpl.h"
#include "http/v2/Http2Request.h"
#include "http/v2/Http2Response.h"
#include "proxy/ProxyConnectionImpl.h"
#include "libkev/src/util/kmtrace.h"
#include "util/util.h"

#ifdef KUMA_HAS_OPENSSL
#include "ssl/OpenSslLib.h"
#endif
#include "DnsResolver.h"

#include "kmapi.h"

KEV_NS_USING

KUMA_NS_BEGIN

template <typename Impl>
struct ImplHelper {
    using ImplPtr = std::shared_ptr<Impl>;
    Impl impl;
    ImplPtr ptr;

    template <typename... Args>
    ImplHelper(Args&... args)
        : impl(args...)
    {
        ptr.reset(&impl, [](Impl* p) {
            auto h = reinterpret_cast<ImplHelper*>(p);
            delete h;
        });
    }

    template <typename... Args>
    ImplHelper(Args&&... args)
        : impl(std::forward<Args...>(args)...)
    {
        ptr.reset(&impl, [](Impl* p) {
            auto h = reinterpret_cast<ImplHelper*>(p);
            delete h;
        });
    }
    
    template <typename... Args>
    static Impl* create(Args&... args)
    {
        auto *ih = new ImplHelper(args...);
        return &ih->impl;
    }
    
    template <typename... Args>
    static Impl* create(Args&&... args)
    {
        auto *ih = new ImplHelper(std::forward<Args...>(args)...);
        return &ih->impl;
    }
    
    static void destroy(Impl *pimpl)
    {
        if (pimpl) {
            auto *ih = reinterpret_cast<ImplHelper*>(pimpl);
            ih->ptr.reset();
        }
    }
    
    static ImplPtr implPtr(Impl *pimpl)
    {
        if (pimpl) {
            auto h = reinterpret_cast<ImplHelper*>(pimpl);
            return h->ptr;
        }
        return ImplPtr();
    }

private:
    ~ImplHelper() = default;
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////
// class EventLoop
using EventLoopHelper = ImplHelper<EventLoop::Impl>;
EventLoop::EventLoop(PollType poll_type)
: pimpl_(EventLoopHelper::create(std::move(poll_type)))
{
    
}

EventLoop::EventLoop(EventLoop &&other)
: pimpl_(std::exchange(other.pimpl_, nullptr))
{
    
}

EventLoop::~EventLoop()
{
    EventLoopHelper::destroy(pimpl_);
}

EventLoop& EventLoop::operator=(EventLoop &&other)
{
    if (this != &other) {
        if (pimpl_) {
            EventLoopHelper::destroy(pimpl_);
        }
        pimpl_ = std::exchange(other.pimpl_, nullptr);
    }
    
    return *this;
}

bool EventLoop::init()
{
    return pimpl_->init();
}

EventLoop::Token EventLoop::createToken()
{
    Token t;
    if (!t.pimpl_) { // lazy initialize token pimpl
        t.pimpl_ = new Token::Impl();
    }
    t.pimpl()->eventLoop(EventLoopHelper::implPtr(pimpl()));
    return t;
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
    return toKMError(pimpl_->registerFd(fd, events, std::move(cb)));
}

KMError EventLoop::updateFd(SOCKET_FD fd, uint32_t events)
{
    return toKMError(pimpl_->updateFd(fd, events));
}

KMError EventLoop::unregisterFd(SOCKET_FD fd, bool close_fd)
{
    return toKMError(pimpl_->unregisterFd(fd, close_fd));
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

bool EventLoop::inSameThread() const
{
    return pimpl_->inSameThread();
}

KMError EventLoop::sync(Task task)
{
    return toKMError(pimpl_->sync(std::move(task)));
}

KMError EventLoop::async(Task task, Token *token)
{
    return toKMError(pimpl_->async(std::move(task), token?token->pimpl():nullptr));
}

KMError EventLoop::post(Task task, Token *token)
{
    return toKMError(pimpl_->post(std::move(task), token?token->pimpl():nullptr));
}

void EventLoop::wakeup()
{
    pimpl_->wakeup();
}

void EventLoop::cancel(Token *token)
{
    if (token) {
        pimpl_->removeTask(token->pimpl());
    }
}

EventLoop::Token::Token()
: pimpl_(nullptr) // lazy initialize pimpl_
{
    
}

EventLoop::Token::Token(Token &&other)
: pimpl_(std::exchange(other.pimpl_, nullptr))
{
    
}

EventLoop::Token::~Token()
{
    delete pimpl_;
}

EventLoop::Token& EventLoop::Token::operator=(Token &&other)
{
    if (this != &other) {
        if (pimpl_) {
            delete pimpl_;
        }
        pimpl_ = std::exchange(other.pimpl_, nullptr);
    }
    return *this;
}

void EventLoop::Token::reset()
{
    if (pimpl_) {
        pimpl_->reset();
    }
}

EventLoop::Token::Impl* EventLoop::Token::pimpl()
{
    return pimpl_;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////
TcpSocket::TcpSocket(EventLoop* loop)
: pimpl_(new Impl(EventLoopHelper::implPtr(loop->pimpl())))
{

}

TcpSocket::TcpSocket(TcpSocket &&other)
: pimpl_(other.pimpl_)
{
    other.pimpl_ = nullptr;
}

TcpSocket::~TcpSocket()
{
    delete pimpl_;
}

TcpSocket& TcpSocket::operator=(TcpSocket &&other)
{
    if (this != &other) {
        if (pimpl_) {
            pimpl_->close();
            delete pimpl_;
        }
        pimpl_ = other.pimpl_;
        other.pimpl_ = nullptr;
    }
    
    return *this;
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
    if (!server_name) {
        return KMError::INVALID_PARAM;
    }
    return pimpl_->setSslServerName(server_name);
#else
    return KMError::NOT_SUPPORTED;
#endif
}

KMError TcpSocket::bind(const char* bind_host, uint16_t bind_port)
{
    if (!bind_host) {
        return KMError::INVALID_PARAM;
    }
    return pimpl_->bind(bind_host, bind_port);
}

KMError TcpSocket::connect(const char* host, uint16_t port, EventCallback cb, uint32_t timeout)
{
    if (!host) {
        return KMError::INVALID_PARAM;
    }
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

KMError TcpSocket::startSslHandshake(SslRole ssl_role, EventCallback cb)
{
#ifdef KUMA_HAS_OPENSSL
    return pimpl_->startSslHandshake(ssl_role, std::move(cb));
#else
    return KMError::NOT_SUPPORTED;
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
    return KMError::NOT_SUPPORTED;
#endif
}

int TcpSocket::send(const void* data, size_t length)
{
    return pimpl_->send(data, length);
}

int TcpSocket::send(const iovec* iovs, int count)
{
    return pimpl_->send(iovs, count);
}

int TcpSocket::send(const KMBuffer &buf)
{
    return pimpl_->send(buf);
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
: pimpl_(new Impl(EventLoopHelper::implPtr(loop->pimpl())))
{

}

TcpListener::TcpListener(TcpListener &&other)
: pimpl_(other.pimpl_)
{
    other.pimpl_ = nullptr;
}

TcpListener::~TcpListener()
{
    delete pimpl_;
}

TcpListener& TcpListener::operator=(TcpListener &&other)
{
    if (this != &other) {
        if (pimpl_) {
            pimpl_->close();
            delete pimpl_;
        }
        pimpl_ = other.pimpl_;
        other.pimpl_ = nullptr;
    }
    
    return *this;
}

KMError TcpListener::startListen(const char* host, uint16_t port)
{
    if (!host) {
        return KMError::INVALID_PARAM;
    }
    return pimpl_->startListen(host, port);
}

KMError TcpListener::stopListen(const char* host, uint16_t port)
{
    return pimpl_->stopListen(host ? host : "", port);
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
: pimpl_(new Impl(EventLoopHelper::implPtr(loop->pimpl())))
{
    
}

UdpSocket::UdpSocket(UdpSocket &&other)
: pimpl_(other.pimpl_)
{
    other.pimpl_ = nullptr;
}

UdpSocket::~UdpSocket()
{
    delete pimpl_;
}

UdpSocket& UdpSocket::operator=(UdpSocket &&other)
{
    if (this != &other) {
        if (pimpl_) {
            pimpl_->close();
            delete pimpl_;
        }
        pimpl_ = other.pimpl_;
        other.pimpl_ = nullptr;
    }
    
    return *this;
}

KMError UdpSocket::bind(const char* bind_host, uint16_t bind_port, uint32_t udp_flags)
{
    if (!bind_host) {
        return KMError::INVALID_PARAM;
    }
    return pimpl_->bind(bind_host, bind_port, udp_flags);
}

int UdpSocket::send(const void* data, size_t length, const char* host, uint16_t port)
{
    return pimpl_->send(data, length, host, port);
}

int UdpSocket::send(const iovec* iovs, int count, const char* host, uint16_t port)
{
    return pimpl_->send(iovs, count, host, port);
}

int UdpSocket::send(const KMBuffer &buf, const char* host, uint16_t port)
{
    return pimpl_->send(buf, host, port);
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
    if (!mcast_addr) {
        return KMError::INVALID_PARAM;
    }
    return pimpl_->mcastJoin(mcast_addr, mcast_port);
}

KMError UdpSocket::mcastLeave(const char* mcast_addr, uint16_t mcast_port)
{
    if (!mcast_addr) {
        return KMError::INVALID_PARAM;
    }
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

Timer::Timer(Timer &&other)
: pimpl_(std::exchange(other.pimpl_, nullptr))
{
    
}

Timer::~Timer()
{
    delete pimpl_;
}

Timer& Timer::operator=(Timer &&other)
{
    if (this != &other) {
        if (pimpl_) {
            delete pimpl_;
        }
        pimpl_ = std::exchange(other.pimpl_, nullptr);
    }
    
    return *this;
}

bool Timer::schedule(uint32_t delay_ms, TimerMode mode, TimerCallback cb)
{
    return pimpl_->schedule(delay_ms, mode, std::move(cb));
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

HttpParser::HttpParser(HttpParser &&other)
: pimpl_(other.pimpl_)
{
    other.pimpl_ = nullptr;
}

HttpParser::~HttpParser()
{
    delete pimpl_;
}

HttpParser& HttpParser::operator=(HttpParser &&other)
{
    if (this != &other) {
        if (pimpl_) {
            delete pimpl_;
        }
        pimpl_ = other.pimpl_;
        other.pimpl_ = nullptr;
    }
    
    return *this;
}

// return bytes parsed
int HttpParser::parse(const char* data, size_t len)
{
    return pimpl_->parse(data, len);
}

int HttpParser::parse(const KMBuffer &buf)
{
    return pimpl_->parse(buf);
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

const char* HttpParser::getUrlQuery() const
{
    return pimpl_->getUrlQuery().c_str();
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

void HttpParser::forEachParam(const EnumerateCallback &cb) const
{
    pimpl_->forEachParam([&cb](const std::string& name, const std::string& value) {
        return cb(name.c_str(), value.c_str());
    });
}

void HttpParser::forEachHeader(const EnumerateCallback &cb) const
{
    pimpl_->forEachHeader([&cb](const std::string& name, const std::string& value) {
        return cb(name.c_str(), value.c_str());
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
    auto loop_ptr = EventLoopHelper::implPtr(loop->pimpl());
    if (is_equal(ver, VersionHTTP2_0)) {
        pimpl_ = new Http2Request(loop_ptr, ver);
    } else {
        pimpl_ = new Http1xRequest(loop_ptr, ver);
    }
}

HttpRequest::HttpRequest(HttpRequest &&other)
: pimpl_(other.pimpl_)
{
    other.pimpl_ = nullptr;
}

HttpRequest::~HttpRequest()
{
    delete pimpl_;
}

HttpRequest& HttpRequest::operator=(HttpRequest &&other)
{
    if (this != &other) {
        if (pimpl_) {
            pimpl_->close();
            delete pimpl_;
        }
        pimpl_ = other.pimpl_;
        other.pimpl_ = nullptr;
    }
    
    return *this;
}

KMError HttpRequest::setSslFlags(uint32_t ssl_flags)
{
    return pimpl_->setSslFlags(ssl_flags);
}

KMError HttpRequest::setProxyInfo(const char *proxy_url, const char *domain_user, const char *passwd)
{
    if (proxy_url && proxy_url[0]) {
        return pimpl_->setProxyInfo({proxy_url, domain_user?domain_user:"", passwd?passwd:""});
    } else {
        return KMError::INVALID_PARAM;
    }
}

KMError HttpRequest::addHeader(const char* name, const char* value)
{
    if (!name || !value) {
        return KMError::INVALID_PARAM;
    }
    return pimpl_->addHeader(name, value);
}

KMError HttpRequest::addHeader(const char* name, uint32_t value)
{
    if (!name) {
        return KMError::INVALID_PARAM;
    }
    return pimpl_->addHeader(name, value);
}

KMError HttpRequest::sendRequest(const char* method, const char* url)
{
    if (!method || !url) {
        return KMError::INVALID_PARAM;
    }
    return pimpl_->sendRequest(method, url);
}

int HttpRequest::sendData(const void* data, size_t len)
{
    return pimpl_->sendData(data, len);
}

int HttpRequest::sendData(const KMBuffer &buf)
{
    return pimpl_->sendData(buf);
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

void HttpRequest::forEachHeader(const EnumerateCallback &cb) const
{
    pimpl_->forEachHeader([&cb] (const std::string& name, const std::string& value) {
        return cb(name.c_str(), value.c_str());
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
    auto loop_ptr = EventLoopHelper::implPtr(loop->pimpl());
    if (is_equal(ver, VersionHTTP2_0)) {
        pimpl_ = new Http2Response(loop_ptr, ver);
    } else {
        pimpl_ = new Http1xResponse(loop_ptr, ver);
    }
}

HttpResponse::HttpResponse(HttpResponse &&other)
: pimpl_(other.pimpl_)
{
    other.pimpl_ = nullptr;
}

HttpResponse::~HttpResponse()
{
    delete pimpl_;
}

HttpResponse& HttpResponse::operator=(HttpResponse &&other)
{
    if (this != &other) {
        if (pimpl_) {
            pimpl_->close();
            delete pimpl_;
        }
        pimpl_ = other.pimpl_;
        other.pimpl_ = nullptr;
    }
    
    return *this;
}

KMError HttpResponse::setSslFlags(uint32_t ssl_flags)
{
    return pimpl_->setSslFlags(ssl_flags);
}

KMError HttpResponse::attachFd(SOCKET_FD fd, const KMBuffer *init_buf)
{
    return pimpl_->attachFd(fd, init_buf);
}

KMError HttpResponse::attachSocket(TcpSocket &&tcp, HttpParser &&parser, const KMBuffer *init_buf)
{
    return pimpl_->attachSocket(std::move(*tcp.pimpl()), std::move(*parser.pimpl()), init_buf);
}

KMError HttpResponse::attachStream(uint32_t stream_id, H2Connection *conn)
{
    return pimpl_->attachStream(stream_id, conn->pimpl());
}

KMError HttpResponse::addHeader(const char* name, const char* value)
{
    if (!name || !value) {
        return KMError::INVALID_PARAM;
    }
    return pimpl_->addHeader(name, value);
}

KMError HttpResponse::addHeader(const char* name, uint32_t value)
{
    if (!name) {
        return KMError::INVALID_PARAM;
    }
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

int HttpResponse::sendData(const KMBuffer &buf)
{
    return pimpl_->sendData(buf);
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

const char* HttpResponse::getQuery() const
{
    return pimpl_->getQuery().c_str();
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

void HttpResponse::forEachHeader(const EnumerateCallback &cb) const
{
    pimpl_->forEachHeader([&cb] (const std::string& name, const std::string& value) {
        return cb(name.c_str(), value.c_str());
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

WebSocket::WebSocket(EventLoop* loop, const char *http_ver)
: pimpl_(new Impl(EventLoopHelper::implPtr(loop->pimpl()), http_ver))
{
    
}

WebSocket::WebSocket(WebSocket &&other)
: pimpl_(other.pimpl_)
{
    other.pimpl_ = nullptr;
}

WebSocket::~WebSocket()
{
    delete pimpl_;
}

WebSocket& WebSocket::operator=(WebSocket &&other)
{
    if (this != &other) {
        if (pimpl_) {
            pimpl_->close();
            delete pimpl_;
        }
        pimpl_ = other.pimpl_;
        other.pimpl_ = nullptr;
    }
    
    return *this;
}

KMError WebSocket::setSslFlags(uint32_t ssl_flags)
{
    return pimpl_->setSslFlags(ssl_flags);
}

void WebSocket::setOrigin(const char* origin)
{
    if (!origin) {
        return;
    }
    pimpl_->setOrigin(origin);
}

const char* WebSocket::getOrigin() const
{
    return pimpl_->getOrigin().c_str();
}

KMError WebSocket::setSubprotocol(const char* subprotocol)
{
    if (!subprotocol) {
        return KMError::INVALID_PARAM;
    }
    return pimpl_->setSubprotocol(subprotocol);
}

const char* WebSocket::getSubprotocol() const
{
    return pimpl_->getSubprotocol().c_str();
}

const char* WebSocket::getExtensions() const
{
    return pimpl_->getExtensions().c_str();
}

KMError WebSocket::setProxyInfo(const char *proxy_url, const char *domain_user, const char *passwd)
{
    if (proxy_url && proxy_url[0]) {
        return pimpl_->setProxyInfo({proxy_url, domain_user?domain_user:"", passwd?passwd:""});
    } else {
        return KMError::INVALID_PARAM;
    }
}

KMError WebSocket::addHeader(const char *name, const char *value)
{
    if (!name || !value) {
        return KMError::INVALID_PARAM;
    }
    return pimpl_->addHeader(name, value);
}

KMError WebSocket::addHeader(const char *name, uint32_t value)
{
    if (!name) {
        return KMError::INVALID_PARAM;
    }
    return pimpl_->addHeader(name, value);
}

KMError WebSocket::connect(const char* ws_url)
{
    if (!ws_url) {
        return KMError::INVALID_PARAM;
    }
    return pimpl_->connect(ws_url);
}

KMError WebSocket::attachFd(SOCKET_FD fd, const KMBuffer *init_buf, HandshakeCallback cb)
{
    return pimpl_->attachFd(fd, init_buf, std::move(cb));
}

KMError WebSocket::attachSocket(TcpSocket &&tcp, HttpParser &&parser, const KMBuffer *init_buf, HandshakeCallback cb)
{
    return pimpl_->attachSocket(std::move(*tcp.pimpl()), std::move((*parser.pimpl())), init_buf, std::move(cb));
}

KMError WebSocket::attachStream(uint32_t stream_id, H2Connection *conn, HandshakeCallback cb)
{
    return pimpl_->attachStream(stream_id, conn->pimpl(), std::move(cb));
}

int WebSocket::send(const void* data, size_t len, bool is_text, bool is_fin, uint32_t flags)
{
    return pimpl_->send(data, len, is_text, is_fin, flags);
}

int WebSocket::send(const KMBuffer &buf, bool is_text, bool is_fin, uint32_t flags)
{
    return pimpl_->send(buf, is_text, is_fin, flags);
}

KMError WebSocket::close()
{
    return pimpl_->close();
}

const char* WebSocket::getPath() const
{
    return pimpl_->getPath().c_str();
}

const char* WebSocket::getHeaderValue(const char* name) const
{
    return pimpl_->getHeaderValue(name).c_str();
}

void WebSocket::forEachHeader(const EnumerateCallback &cb) const
{
    pimpl_->forEachHeader([&cb] (const std::string& name, const std::string& value) {
        return cb(name.c_str(), value.c_str());
    });
}

void WebSocket::setOpenCallback(EventCallback cb)
{
    pimpl_->setOpenCallback(std::move(cb));
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
ProxyConnection::ProxyConnection(EventLoop* loop)
: pimpl_(new Impl(EventLoopHelper::implPtr(loop->pimpl())))
{
    
}

ProxyConnection::ProxyConnection(ProxyConnection &&other)
: pimpl_(other.pimpl_)
{
    other.pimpl_ = nullptr;
}

ProxyConnection::~ProxyConnection()
{
    delete pimpl_;
}

ProxyConnection& ProxyConnection::operator=(ProxyConnection &&other)
{
    if (this != &other) {
        if (pimpl_) {
            pimpl_->close();
            delete pimpl_;
        }
        pimpl_ = other.pimpl_;
        other.pimpl_ = nullptr;
    }
    
    return *this;
}

KMError ProxyConnection::setSslFlags(uint32_t ssl_flags)
{
    return pimpl_->setSslFlags(ssl_flags);
}

uint32_t ProxyConnection::getSslFlags() const
{
    return pimpl_->getSslFlags();
}

bool ProxyConnection::sslEnabled() const
{
    return pimpl_->sslEnabled();
}

KMError ProxyConnection::setSslServerName(const char *server_name)
{
#ifdef KUMA_HAS_OPENSSL
    if (server_name) {
        return pimpl_->setSslServerName(server_name);
    } else {
        return KMError::INVALID_PARAM;
    }
#else
    return KMError::NOT_SUPPORTED;
#endif
}

KMError ProxyConnection::setProxyInfo(const char *proxy_url, const char *domain_user, const char *passwd)
{
    if (proxy_url && proxy_url[0]) {
        return pimpl_->setProxyInfo({proxy_url, domain_user?domain_user:"", passwd?passwd:""});
    } else {
        return KMError::INVALID_PARAM;
    }
}

KMError ProxyConnection::connect(const char *host, uint16_t port, EventCallback cb)
{
    if (host) {
        return pimpl_->connect(host, port, std::move(cb));
    } else {
        return KMError::INVALID_PARAM;
    }
}

int ProxyConnection::send(const void* data, size_t len)
{
    return pimpl_->send(data, len);
}

int ProxyConnection::send(const iovec* iovs, int count)
{
    return pimpl_->send(iovs, count);
}

int ProxyConnection::send(const KMBuffer &buf)
{
    return pimpl_->send(buf);
}

KMError ProxyConnection::close()
{
    return pimpl_->close();
}

void ProxyConnection::setDataCallback(DataCallback cb)
{
    pimpl_->setDataCallback(std::move(cb));
}

void ProxyConnection::setWriteCallback(EventCallback cb)
{
    pimpl_->setWriteCallback(std::move(cb));
}

void ProxyConnection::setErrorCallback(EventCallback cb)
{
    pimpl_->setErrorCallback(std::move(cb));
}

bool ProxyConnection::canSendData() const
{
    return pimpl_->canSendData();
}

bool ProxyConnection::sendBufferEmpty() const
{
    return pimpl_->sendBufferEmpty();
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////
H2Connection::H2Connection(EventLoop* loop)
: pimpl_(new Impl(EventLoopHelper::implPtr(loop->pimpl())))
{
    
}

H2Connection::H2Connection(H2Connection &&other)
: pimpl_(other.pimpl_)
{
    other.pimpl_ = nullptr;
}

H2Connection::~H2Connection()
{
    delete pimpl_;
}

H2Connection& H2Connection::operator=(H2Connection &&other)
{
    if (this != &other) {
        if (pimpl_) {
            pimpl_->close();
            delete pimpl_;
        }
        pimpl_ = other.pimpl_;
        other.pimpl_ = nullptr;
    }
    
    return *this;
}

KMError H2Connection::setSslFlags(uint32_t ssl_flags)
{
    return pimpl_->setSslFlags(ssl_flags);
}
/*
KMError H2Connection::connect(const char* host, uint16_t port, ConnectCallback cb)
{
    if (!host) {
        return KMError::INVALID_PARAM;
    }
    return pimpl_->connect(host, port, cb);
}
*/
KMError H2Connection::attachFd(SOCKET_FD fd, const KMBuffer *init_buf)
{
    return pimpl_->attachFd(fd, init_buf);
}

KMError H2Connection::attachSocket(TcpSocket &&tcp, HttpParser &&parser, const KMBuffer *init_buf)
{
    return pimpl_->attachSocket(std::move(*tcp.pimpl()), std::move(*parser.pimpl()), init_buf);
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

void setLogCallback(LogCallback cb)
{
    setTraceFunc(cb);
}

KUMA_NS_END


#ifdef KUMA_OS_WIN

BOOL WINAPI DllMain(HINSTANCE module_handle, DWORD reason_for_call, LPVOID reserved)
{
    switch (reason_for_call)
    {
        case DLL_PROCESS_ATTACH:
            //DisableThreadLibraryCalls(module_handle);
            break;
        case DLL_PROCESS_DETACH:
            break;
    }
    return TRUE;
}

#endif // KUMA_OS_WIN
