/* Copyright (c) 2014, Fengping Bao <jamol@live.com>
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef __KUMAAPI_H__
#define __KUMAAPI_H__

#include "kmdefs.h"
#include "evdefs.h"

#include <stdint.h>
#ifdef KUMA_OS_WIN
# include <Ws2tcpip.h>
#else
# include <sys/socket.h>
#endif

KUMA_NS_BEGIN

class EventLoopImpl;
class TcpSocketImpl;
class UdpSocketImpl;
class TcpListenerImpl;
class TimerImpl;
class HttpParserImpl;
class IHttpRequest;
class HttpResponseImpl;
class WebSocketImpl;
class H2ConnectionImpl;

class KUMA_API EventLoop
{
public:
    EventLoop(PollType poll_type = POLL_TYPE_NONE);
    ~EventLoop();
    
public:
    bool init();
    int registerFd(SOCKET_FD fd, uint32_t events, IOCallback cb);
    int updateFd(SOCKET_FD fd, uint32_t events);
    int unregisterFd(SOCKET_FD fd, bool close_fd);
    
    PollType getPollType() const;
    bool isPollLT() const; // level trigger
    
public:
    bool isInEventLoopThread() const;
    int runInEventLoop(LoopCallback cb);
    int runInEventLoopSync(LoopCallback cb);
    int queueInEventLoop(LoopCallback cb);
    void loopOnce(uint32_t max_wait_ms);
    void loop(uint32_t max_wait_ms = -1);
    void stop();
    EventLoopImpl* pimpl();
    
private:
    EventLoopImpl*  pimpl_;
};

class KUMA_API TcpSocket
{
public:
    typedef std::function<void(int)> EventCallback;
    
    TcpSocket(EventLoop* loop);
    ~TcpSocket();
    
    int setSslFlags(uint32_t ssl_flags);
    uint32_t getSslFlags() const ;
    bool sslEnabled() const;
    int bind(const char* bind_host, uint16_t bind_port);
    int connect(const char* host, uint16_t port, EventCallback cb, uint32_t timeout_ms = 0);
    int attachFd(SOCKET_FD fd);
    int detachFd(SOCKET_FD &fd);
    int startSslHandshake(SslRole ssl_role);
    int getAlpnSelected(char *buf, size_t len);
    int send(const uint8_t* data, size_t length);
    int send(iovec* iovs, int count);
    int receive(uint8_t* data, size_t length);
    int close();
    
    int pause();
    int resume();
    
    void setReadCallback(EventCallback cb);
    void setWriteCallback(EventCallback cb);
    void setErrorCallback(EventCallback cb);
    
    SOCKET_FD getFd() const;
    TcpSocketImpl* pimpl();
    
private:
    TcpSocketImpl* pimpl_;
};

class KUMA_API TcpListener
{
public:
    typedef std::function<void(SOCKET_FD, const char*, uint16_t)> ListenCallback;
    typedef std::function<void(int)> ErrorCallback;
    
    TcpListener(EventLoop* loop);
    ~TcpListener();
    
    int startListen(const char* host, uint16_t port);
    int stopListen(const char* host, uint16_t port);
    int close();
    
    void setListenCallback(ListenCallback cb);
    void setErrorCallback(ErrorCallback cb);
    TcpListenerImpl* pimpl();
    
private:
    TcpListenerImpl* pimpl_;
};

class KUMA_API UdpSocket
{
public:
    typedef std::function<void(int)> EventCallback;
    
    UdpSocket(EventLoop* loop);
    ~UdpSocket();
    
    int bind(const char* bind_host, uint16_t bind_port, uint32_t udp_flags=0);
    int send(const uint8_t* data, size_t length, const char* host, uint16_t port);
    int send(iovec* iovs, int count, const char* host, uint16_t port);
    int receive(uint8_t* data, size_t length, char* ip, size_t ip_len, uint16_t& port);
    int close();
    
    int mcastJoin(const char* mcast_addr, uint16_t mcast_port);
    int mcastLeave(const char* mcast_addr, uint16_t mcast_port);
    
    void setReadCallback(EventCallback cb);
    void setErrorCallback(EventCallback cb);
    UdpSocketImpl* pimpl();
    
private:
    UdpSocketImpl* pimpl_;
};

class KUMA_API Timer
{
public:
    typedef std::function<void(void)> TimerCallback;
    
    Timer(EventLoop* loop);
    ~Timer();
    
    bool schedule(uint32_t delay_ms, TimerCallback cb, TimerMode mode=ONE_SHOT);
    void cancel();
    TimerImpl* pimpl();
    
private:
    TimerImpl* pimpl_;
};

class KUMA_API HttpParser
{
public:
    typedef std::function<void(const char*, size_t)> DataCallback;
    typedef std::function<void(HttpEvent)> EventCallback;
    typedef std::function<void(const char*, const char*)> EnumrateCallback;
    
    HttpParser();
    ~HttpParser();
    
    // return bytes parsed
    int parse(const char* data, size_t len);
    void pause();
    void resume();
    
    // true - http completed
    bool setEOF();
    void reset();
    
    bool isRequest() const;
    bool headerComplete() const;
    bool complete() const;
    bool error() const;
    bool paused() const;
    
    int getStatusCode() const;
    const char* getUrl() const;
    const char* getUrlPath() const;
    const char* getMethod() const;
    const char* getVersion() const;
    const char* getParamValue(const char* name) const;
    const char* getHeaderValue(const char* name) const;
    
    void forEachParam(EnumrateCallback cb);
    void forEachHeader(EnumrateCallback cb);
    
    void setDataCallback(DataCallback cb);
    void setEventCallback(EventCallback cb);
    
    HttpParserImpl* pimpl();
    
private:
    HttpParserImpl* pimpl_;
};

class KUMA_API HttpRequest
{
public:
    typedef std::function<void(uint8_t*, size_t)> DataCallback;
    typedef std::function<void(int)> EventCallback;
    typedef std::function<void(void)> HttpEventCallback;
    
    HttpRequest(EventLoop* loop, const char* ver = "HTTP/1.1");
    ~HttpRequest();
    
    int setSslFlags(uint32_t ssl_flags);
    void addHeader(const char* name, const char* value);
    void addHeader(const char* name, uint32_t value);
    int sendRequest(const char* method, const char* url);
    int sendData(const uint8_t* data, size_t len);
    void reset(); // reset for connection reuse
    int close();
    
    int getStatusCode() const;
    const char* getVersion() const;
    const char* getHeaderValue(const char* name) const;
    void forEachHeader(HttpParser::EnumrateCallback cb);
    
    void setDataCallback(DataCallback cb);
    void setWriteCallback(EventCallback cb);
    void setErrorCallback(EventCallback cb);
    void setHeaderCompleteCallback(HttpEventCallback cb);
    void setResponseCompleteCallback(HttpEventCallback cb);
    
    IHttpRequest* pimpl();
    
private:
    IHttpRequest* pimpl_;
    char ver_[9] = {0};
};

class KUMA_API HttpResponse
{
public:
    typedef std::function<void(uint8_t*, size_t)> DataCallback;
    typedef std::function<void(int)> EventCallback;
    typedef std::function<void(void)> HttpEventCallback;
    
    HttpResponse(EventLoop* loop);
    ~HttpResponse();
    
    int setSslFlags(uint32_t ssl_flags);
    int attachFd(SOCKET_FD fd, uint8_t* init_data=nullptr, size_t init_len=0);
    int attachSocket(TcpSocket&& tcp, HttpParser&& parser);
    void addHeader(const char* name, const char* value);
    void addHeader(const char* name, uint32_t value);
    int sendResponse(int status_code, const char* desc = nullptr, const char* ver = "HTTP/1.1");
    int sendData(const uint8_t* data, size_t len);
    void reset(); // reset for connection reuse
    int close();
    
    const char* getMethod() const;
    const char* getUrl() const;
    const char* getVersion() const;
    const char* getParamValue(const char* name) const;
    const char* getHeaderValue(const char* name) const;
    void forEachHeader(HttpParser::EnumrateCallback cb);
    
    void setDataCallback(DataCallback cb);
    void setWriteCallback(EventCallback cb);
    void setErrorCallback(EventCallback cb);
    void setHeaderCompleteCallback(HttpEventCallback cb);
    void setRequestCompleteCallback(HttpEventCallback cb);
    void setResponseCompleteCallback(HttpEventCallback cb);
    
    HttpResponseImpl* pimpl();
    
private:
    HttpResponseImpl* pimpl_;
};

class KUMA_API WebSocket
{
public:
    typedef std::function<void(uint8_t*, size_t)> DataCallback;
    typedef std::function<void(int)> EventCallback;
    
    WebSocket(EventLoop* loop);
    ~WebSocket();
    
    int setSslFlags(uint32_t ssl_flags);
    void setProtocol(const char* proto);
    const char* getProtocol() const;
    void setOrigin(const char* origin);
    const char* getOrigin() const;
    int connect(const char* ws_url, EventCallback cb);
    int attachFd(SOCKET_FD fd, const uint8_t* init_data=nullptr, size_t init_len=0);
    int attachSocket(TcpSocket&& tcp, HttpParser&& parser);
    int send(const uint8_t* data, size_t len);
    int close();
    
    void setDataCallback(DataCallback cb);
    void setWriteCallback(EventCallback cb);
    void setErrorCallback(EventCallback cb);
    
    WebSocketImpl* pimpl();
    
private:
    WebSocketImpl* pimpl_;
};

class KUMA_API H2Connection
{
public:
    using ConnectCallback = std::function<void(int)>;
    H2Connection(EventLoop* loop);
    ~H2Connection();
    
    int setSslFlags(uint32_t ssl_flags);
    int connect(const char* host, uint16_t port, ConnectCallback cb);
    int attachFd(SOCKET_FD fd, const uint8_t* data=nullptr, size_t size=0);
    int attachSocket(TcpSocket&& tcp, HttpParser&& parser);
    int close();
    
    H2ConnectionImpl* pimpl();
    
private:
    H2ConnectionImpl* pimpl_;
};

KUMA_API void init(const char* path = nullptr);
KUMA_API void fini();
KUMA_API void setTraceFunc(std::function<void(int, const char*)> func);

KUMA_NS_END

#endif
