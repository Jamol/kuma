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

class KUMA_API EventLoop
{
public:
    EventLoop(PollType poll_type = PollType::NONE);
    ~EventLoop();
    
public:
    bool init();
    
    /* NOTE: cb must be valid untill unregisterFd called
     */
    KMError registerFd(SOCKET_FD fd, uint32_t events, IOCallback cb);
    KMError updateFd(SOCKET_FD fd, uint32_t events);
    KMError unregisterFd(SOCKET_FD fd, bool close_fd);
    
    PollType getPollType() const;
    bool isPollLT() const; // level trigger
    
public:
    bool inSameThread() const;
    
    /* run the cb in loop thread and wait untill cb is executed.
     * cb will execute at once if called on loop thread
     *
     * @param cb callback to be executed. it will always be executed when call success
     */
    KMError sync(LoopCallback cb);
    
    /* run the cb in loop thread.
     * cb will execute at once if called on loop thread
     *
     * @param cb callback to be executed. it will always be executed when call success
     */
    KMError async(LoopCallback cb);
    
    /* run the cb in loop thread at next time.
     *
     * @param cb callback to be executed. it will always be executed when call success
     */
    KMError queue(LoopCallback cb);
    void loopOnce(uint32_t max_wait_ms);
    void loop(uint32_t max_wait_ms = -1);
    void stop();
    
    class Impl;
    Impl* pimpl();

private:
    Impl*  pimpl_;
};

class KUMA_API TcpSocket
{
public:
    using EventCallback = std::function<void(KMError)>;
    
    TcpSocket(EventLoop* loop);
    ~TcpSocket();
    
    KMError setSslFlags(uint32_t ssl_flags);
    uint32_t getSslFlags() const;
    bool sslEnabled() const;
    KMError setSslServerName(const char *server_name);
    KMError bind(const char* bind_host, uint16_t bind_port);
    KMError connect(const char* host, uint16_t port, EventCallback cb, uint32_t timeout_ms = 0);
    KMError attachFd(SOCKET_FD fd);
    KMError detachFd(SOCKET_FD &fd);
    KMError startSslHandshake(SslRole ssl_role);
    KMError getAlpnSelected(char *buf, size_t len);
    int send(const void* data, size_t length);
    int send(iovec* iovs, int count);
    int receive(void* data, size_t length);
    KMError close();
    
    KMError pause();
    KMError resume();
    
    /* NOTE: cb must be valid untill close called
     */
    void setReadCallback(EventCallback cb);
    void setWriteCallback(EventCallback cb);
    void setErrorCallback(EventCallback cb);
    
    SOCKET_FD getFd() const;
    
    class Impl;
    Impl* pimpl();
    
private:
    Impl* pimpl_;
};

class KUMA_API TcpListener
{
public:
    using AcceptCallback = std::function<bool(SOCKET_FD, const char*, uint16_t)>;
    using ErrorCallback = std::function<void(KMError)>;
    
    TcpListener(EventLoop* loop);
    ~TcpListener();
    
    KMError startListen(const char* host, uint16_t port);
    KMError stopListen(const char* host, uint16_t port);
    KMError close();
    
    void setAcceptCallback(AcceptCallback cb);
    void setErrorCallback(ErrorCallback cb);
    
    class Impl;
    Impl* pimpl();
    
private:
    Impl* pimpl_;
};

class KUMA_API UdpSocket
{
public:
    using EventCallback = std::function<void(KMError)>;
    
    UdpSocket(EventLoop* loop);
    ~UdpSocket();
    
    KMError bind(const char* bind_host, uint16_t bind_port, uint32_t udp_flags=0);
    int send(const void* data, size_t length, const char* host, uint16_t port);
    int send(iovec* iovs, int count, const char* host, uint16_t port);
    int receive(void* data, size_t length, char* ip, size_t ip_len, uint16_t& port);
    KMError close();
    
    KMError mcastJoin(const char* mcast_addr, uint16_t mcast_port);
    KMError mcastLeave(const char* mcast_addr, uint16_t mcast_port);
    
    void setReadCallback(EventCallback cb);
    void setErrorCallback(EventCallback cb);
    
    class Impl;
    Impl* pimpl();
    
private:
    Impl* pimpl_;
};

class KUMA_API Timer
{
public:
    using TimerCallback = std::function<void(void)>;
    
    Timer(EventLoop* loop);
    ~Timer();
    
    bool schedule(uint32_t delay_ms, TimerCallback cb, TimerMode mode=TimerMode::ONE_SHOT);
    void cancel();
    
    class Impl;
    Impl* pimpl();
    
private:
    Impl* pimpl_;
};

class KUMA_API HttpParser
{
public:
    using DataCallback = std::function<void(void*, size_t)>;
    using EventCallback = std::function<void(HttpEvent)>;
    using EnumrateCallback = std::function<void(const char*, const char*)>;
    
    HttpParser();
    ~HttpParser();
    
    // return bytes parsed
    int parse(char* data, size_t len);
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
    bool isUpgradeTo(const char* proto) const;
    
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
    
    class Impl;
    Impl* pimpl();
    
private:
    Impl* pimpl_;
};

class KUMA_API HttpRequest
{
public:
    using DataCallback = std::function<void(void*, size_t)>;
    using EventCallback = std::function<void(KMError)>;
    using HttpEventCallback = std::function<void(void)>;
    
    /* 
     * @param ver, http version, "HTTP/2.0" for HTTP2
     */
    HttpRequest(EventLoop* loop, const char* ver = "HTTP/1.1");
    ~HttpRequest();
    
    KMError setSslFlags(uint32_t ssl_flags);
    void addHeader(const char* name, const char* value);
    void addHeader(const char* name, uint32_t value);
    KMError sendRequest(const char* method, const char* url);
    int sendData(const void* data, size_t len);
    void reset(); // reset for connection reuse
    KMError close();
    
    int getStatusCode() const;
    const char* getVersion() const;
    const char* getHeaderValue(const char* name) const;
    void forEachHeader(HttpParser::EnumrateCallback cb);
    
    void setDataCallback(DataCallback cb);
    void setWriteCallback(EventCallback cb);
    void setErrorCallback(EventCallback cb);
    void setHeaderCompleteCallback(HttpEventCallback cb);
    void setResponseCompleteCallback(HttpEventCallback cb);
    
    class Impl;
    Impl* pimpl();
    
private:
    Impl* pimpl_;
};

class KUMA_API HttpResponse
{
public:
    using DataCallback = std::function<void(void*, size_t)>;
    using EventCallback = std::function<void(KMError)>;
    using HttpEventCallback = std::function<void(void)>;
    
    /*
     * @param ver, http version, "HTTP/2.0" for HTTP2
     */
    HttpResponse(EventLoop* loop, const char* ver);
    ~HttpResponse();
    
    KMError setSslFlags(uint32_t ssl_flags);
    KMError attachFd(SOCKET_FD fd, const void* init_data=nullptr, size_t init_len=0);
    KMError attachSocket(TcpSocket&& tcp, HttpParser&& parser, const void* init_data=nullptr, size_t init_len=0);
    void addHeader(const char* name, const char* value);
    void addHeader(const char* name, uint32_t value);
    KMError sendResponse(int status_code, const char* desc = nullptr);
    int sendData(const void* data, size_t len);
    void reset(); // reset for connection reuse
    KMError close();
    
    const char* getMethod() const;
    const char* getPath() const;
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
    
    class Impl;
    Impl* pimpl();
    
private:
    Impl* pimpl_;
};

class KUMA_API WebSocket
{
public:
    using DataCallback = std::function<void(void*, size_t)>;
    using EventCallback = std::function<void(KMError)>;
    
    WebSocket(EventLoop* loop);
    ~WebSocket();
    
    KMError setSslFlags(uint32_t ssl_flags);
    void setProtocol(const char* proto);
    const char* getProtocol() const;
    void setOrigin(const char* origin);
    const char* getOrigin() const;
    KMError connect(const char* ws_url, EventCallback cb);
    KMError attachFd(SOCKET_FD fd, const void* init_data=nullptr, size_t init_len=0);
    KMError attachSocket(TcpSocket&& tcp, HttpParser&& parser, const void* init_data=nullptr, size_t init_len=0);
    int send(const void* data, size_t len);
    KMError close();
    
    void setDataCallback(DataCallback cb);
    void setWriteCallback(EventCallback cb);
    void setErrorCallback(EventCallback cb);
    
    class Impl;
    Impl* pimpl();
    
private:
    Impl* pimpl_;
};

class KUMA_API H2Connection
{
public:
    using AcceptCallback = std::function<bool(uint32_t/* stream ID */)>;
    using ErrorCallback = std::function<void(int)>;
    
    H2Connection(EventLoop* loop);
    ~H2Connection();
    
    KMError setSslFlags(uint32_t ssl_flags);
    KMError attachFd(SOCKET_FD fd, const void* init_data=nullptr, size_t init_len=0);
    KMError attachSocket(TcpSocket&& tcp, HttpParser&& parser, const void* init_data=nullptr, size_t init_len=0);
    KMError attachStream(uint32_t streamId, HttpResponse* rsp);
    KMError close();
    void setAcceptCallback(AcceptCallback cb);
    void setErrorCallback(ErrorCallback cb);
    
    class Impl;
    Impl* pimpl();
    
private:
    Impl* pimpl_;
};

using TraceFunc = std::function<void(int, const char*)>; // (level, msg)

KUMA_API void init(const char* path = nullptr);
KUMA_API void fini();
KUMA_API void setTraceFunc(TraceFunc func);

KUMA_NS_END

#endif
