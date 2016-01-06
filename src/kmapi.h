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
class TcpServerSocketImpl;
class TimerImpl;
class HttpParserImpl;
class HttpRequestImpl;
class HttpResponseImpl;
class WebSocketImpl;

class KUMA_API EventLoop
{
public:
    EventLoop(PollType poll_type = POLL_TYPE_NONE);
    ~EventLoop();
    
public:
    bool init();
    int registerFd(SOCKET_FD fd, uint32_t events, IOCallback& cb);
    int registerFd(SOCKET_FD fd, uint32_t events, IOCallback&& cb);
    int updateFd(SOCKET_FD fd, uint32_t events);
    int unregisterFd(SOCKET_FD fd, bool close_fd);
    
    PollType getPollType();
    bool isPollLT(); // level trigger
    
public:
    bool isInEventLoopThread();
    int runInEventLoop(LoopCallback& cb);
    int runInEventLoop(LoopCallback&& cb);
    int runInEventLoopSync(LoopCallback& cb);
    int runInEventLoopSync(LoopCallback&& cb);
    int queueInEventLoop(LoopCallback& cb);
    int queueInEventLoop(LoopCallback&& cb);
    void loopOnce(uint32_t max_wait_ms);
    void loop(uint32_t max_wait_ms = -1);
    void stop();
    EventLoopImpl* getPimpl();
    
private:
    EventLoopImpl*  pimpl_;
};

class KUMA_API TcpSocket
{
public:
    typedef std::function<void(int)> EventCallback;
    
    TcpSocket(EventLoop* loop);
    ~TcpSocket();
    
    int bind(const char* bind_host, uint16_t bind_port);
    int connect(const char* host, uint16_t port, EventCallback& cb, uint32_t flags = 0, uint32_t timeout = 0);
    int connect(const char* host, uint16_t port, EventCallback&& cb, uint32_t flags = 0, uint32_t timeout = 0);
    int attachFd(SOCKET_FD fd, uint32_t flags = 0);
    int detachFd(SOCKET_FD &fd);
    int startSslHandshake(bool is_server);
    int send(const uint8_t* data, uint32_t length);
    int send(iovec* iovs, uint32_t count);
    int receive(uint8_t* data, uint32_t length);
    int close();
    
    int pause();
    int resume();
    
    void setReadCallback(EventCallback& cb);
    void setWriteCallback(EventCallback& cb);
    void setErrorCallback(EventCallback& cb);
    void setReadCallback(EventCallback&& cb);
    void setWriteCallback(EventCallback&& cb);
    void setErrorCallback(EventCallback&& cb);
    
    SOCKET_FD getFd();
    TcpSocketImpl* getPimpl();
    
private:
    TcpSocketImpl* pimpl_;
};

class KUMA_API TcpServerSocket
{
public:
    typedef std::function<void(SOCKET_FD)> AcceptCallback;
    typedef std::function<void(int)> ErrorCallback;
    
    TcpServerSocket(EventLoop* loop);
    ~TcpServerSocket();
    
    int startListen(const char* host, uint16_t port);
    int stopListen(const char* host, uint16_t port);
    int close();
    
    void setAcceptCallback(AcceptCallback& cb);
    void setErrorCallback(ErrorCallback& cb);
    void setAcceptCallback(AcceptCallback&& cb);
    void setErrorCallback(ErrorCallback&& cb);
    TcpServerSocketImpl* getPimpl();
    
private:
    TcpServerSocketImpl* pimpl_;
};

class KUMA_API UdpSocket
{
public:
    typedef std::function<void(int)> EventCallback;
    
    UdpSocket(EventLoop* loop);
    ~UdpSocket();
    
    int bind(const char* bind_host, uint16_t bind_port, uint32_t flags = 0);
    int send(const uint8_t* data, uint32_t length, const char* host, uint16_t port);
    int send(iovec* iovs, uint32_t count, const char* host, uint16_t port);
    int receive(uint8_t* data, uint32_t length, char* ip, uint32_t ip_len, uint16_t& port);
    int close();
    
    int mcastJoin(const char* mcast_addr, uint16_t mcast_port);
    int mcastLeave(const char* mcast_addr, uint16_t mcast_port);
    
    void setReadCallback(EventCallback& cb);
    void setErrorCallback(EventCallback& cb);
    void setReadCallback(EventCallback&& cb);
    void setErrorCallback(EventCallback&& cb);
    UdpSocketImpl* getPimpl();
    
private:
    UdpSocketImpl* pimpl_;
};

class KUMA_API Timer
{
public:
    typedef std::function<void(void)> TimerCallback;
    
    Timer(EventLoop* loop);
    ~Timer();
    
    bool schedule(unsigned int time_elapse, TimerCallback& cb, bool repeat = false);
    bool schedule(unsigned int time_elapse, TimerCallback&& cb, bool repeat = false);
    void cancel();
    TimerImpl* getPimpl();
    
private:
    TimerImpl* pimpl_;
};

class KUMA_API HttpParser
{
public:
    typedef std::function<void(const char*, uint32_t)> DataCallback;
    typedef std::function<void(HttpEvent)> EventCallback;
    typedef std::function<void(const char* name, const char* value)> EnumrateCallback;
    
    HttpParser();
    ~HttpParser();
    
    // return bytes parsed
    int parse(const char* data, uint32_t len);
    void pause();
    void resume();
    
    // true - http completed
    bool setEOF();
    void reset();
    
    bool isRequest();
    bool headerComplete();
    bool complete();
    bool error();
    bool paused();
    
    int getStatusCode();
    const char* getUrl();
    const char* getUrlPath();
    const char* getMethod();
    const char* getVersion();
    const char* getParamValue(const char* name);
    const char* getHeaderValue(const char* name);
    
    void forEachParam(EnumrateCallback& cb);
    void forEachHeader(EnumrateCallback& cb);
    void forEachParam(EnumrateCallback&& cb);
    void forEachHeader(EnumrateCallback&& cb);
    
    void setDataCallback(DataCallback& cb);
    void setEventCallback(EventCallback& cb);
    void setDataCallback(DataCallback&& cb);
    void setEventCallback(EventCallback&& cb);
    
    HttpParserImpl* getPimpl();
    
private:
    HttpParserImpl* pimpl_;
};

class KUMA_API HttpRequest
{
public:
    typedef std::function<void(uint8_t*, uint32_t)> DataCallback;
    typedef std::function<void(int)> EventCallback;
    typedef std::function<void(void)> HttpEventCallback;
    
    HttpRequest(EventLoop* loop);
    ~HttpRequest();
    
    void addHeader(const char* name, const char* value);
    void addHeader(const char* name, uint32_t value);
    int sendRequest(const char* method, const char* url, const char* ver = "HTTP/1.1");
    int sendData(const uint8_t* data, uint32_t len);
    void reset(); // reset for connection reuse
    int close();
    
    int getStatusCode();
    const char* getVersion();
    const char* getHeaderValue(const char* name);
    void forEachHeader(HttpParser::EnumrateCallback& cb);
    void forEachHeader(HttpParser::EnumrateCallback&& cb);
    
    void setDataCallback(DataCallback& cb);
    void setWriteCallback(EventCallback& cb);
    void setErrorCallback(EventCallback& cb);
    void setHeaderCompleteCallback(HttpEventCallback& cb);
    void setResponseCompleteCallback(HttpEventCallback& cb);
    void setDataCallback(DataCallback&& cb);
    void setWriteCallback(EventCallback&& cb);
    void setErrorCallback(EventCallback&& cb);
    void setHeaderCompleteCallback(HttpEventCallback&& cb);
    void setResponseCompleteCallback(HttpEventCallback&& cb);
    
    HttpRequestImpl* getPimpl();
    
private:
    HttpRequestImpl* pimpl_;
};

class KUMA_API HttpResponse
{
public:
    typedef std::function<void(uint8_t*, uint32_t)> DataCallback;
    typedef std::function<void(int)> EventCallback;
    typedef std::function<void(void)> HttpEventCallback;
    
    HttpResponse(EventLoop* loop);
    ~HttpResponse();
    
    int attachFd(SOCKET_FD fd, uint32_t flags=0, uint8_t* init_data=nullptr, uint32_t init_len=0);
    int attachTcp(TcpSocket &tcp, HttpParser&& parser, uint32_t flags=0, uint8_t* init_data=nullptr, uint32_t init_len=0);
    void addHeader(const char* name, const char* value);
    void addHeader(const char* name, uint32_t value);
    int sendResponse(int status_code, const char* desc = nullptr, const char* ver = "HTTP/1.1");
    int sendData(const uint8_t* data, uint32_t len);
    void reset(); // reset for connection reuse
    int close();
    
    const char* getMethod();
    const char* getUrl();
    const char* getVersion();
    const char* getParamValue(const char* name);
    const char* getHeaderValue(const char* name);
    void forEachHeader(HttpParser::EnumrateCallback& cb);
    void forEachHeader(HttpParser::EnumrateCallback&& cb);
    
    void setDataCallback(DataCallback& cb);
    void setWriteCallback(EventCallback& cb);
    void setErrorCallback(EventCallback& cb);
    void setHeaderCompleteCallback(HttpEventCallback& cb);
    void setRequestCompleteCallback(HttpEventCallback& cb);
    void setResponseCompleteCallback(HttpEventCallback& cb);
    void setDataCallback(DataCallback&& cb);
    void setWriteCallback(EventCallback&& cb);
    void setErrorCallback(EventCallback&& cb);
    void setHeaderCompleteCallback(HttpEventCallback&& cb);
    void setRequestCompleteCallback(HttpEventCallback&& cb);
    void setResponseCompleteCallback(HttpEventCallback&& cb);
    
    HttpResponseImpl* getPimpl();
    
private:
    HttpResponseImpl* pimpl_;
};

class KUMA_API WebSocket
{
public:
    typedef std::function<void(uint8_t*, uint32_t)> DataCallback;
    typedef std::function<void(int)> EventCallback;
    
    WebSocket(EventLoop* loop);
    ~WebSocket();
    
    void setProtocol(const char* proto);
    const char* getProtocol();
    void setOrigin(const char* origin);
    const char* getOrigin();
    int connect(const char* ws_url, EventCallback& cb);
    int connect(const char* ws_url, EventCallback&& cb);
    int attachFd(SOCKET_FD fd, uint32_t flags=0, const uint8_t* init_data=nullptr, uint32_t init_len=0);
    int attachTcp(TcpSocket &tcp, HttpParser&& parser, uint32_t flags=0, const uint8_t* init_data=nullptr, uint32_t init_len=0);
    int send(const uint8_t* data, uint32_t len);
    int close();
    
    void setDataCallback(DataCallback& cb);
    void setWriteCallback(EventCallback& cb);
    void setErrorCallback(EventCallback& cb);
    void setDataCallback(DataCallback&& cb);
    void setWriteCallback(EventCallback&& cb);
    void setErrorCallback(EventCallback&& cb);
    
    WebSocketImpl* getPimpl();
    
private:
    WebSocketImpl* pimpl_;
};

KUMA_API void init(const char* path = nullptr);
KUMA_API void fini();
KUMA_API void setTraceFunc(std::function<void(int, const char*)> func);

KUMA_NS_END

#endif
