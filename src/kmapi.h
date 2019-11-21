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
#include "kmbuffer.h"

#include <stdint.h>
#ifdef KUMA_OS_WIN
# include <Ws2tcpip.h>
#else
# include <sys/socket.h>
#endif

KUMA_NS_BEGIN

class KMBuffer;
class H2Connection;

class KUMA_API EventLoop
{
public:
    using Task = std::function<void(void)>;
    
    class Token {
    public:
        Token();
        Token(Token &&other);
        Token(const Token &other) = delete;
        ~Token();
        
        Token& operator=(Token &&other);
        Token& operator=(const Token &other) = delete;
        
        void reset();
        
        class Impl;
        Impl* pimpl();
        
    private:
        Impl* pimpl_;
    };
    
public:
    EventLoop(PollType poll_type = PollType::NONE);
    ~EventLoop();
    
public:
    bool init();
    
    /* NOTE: cb must be valid untill unregisterFd called
     * this API is thread-safe
     */
    KMError registerFd(SOCKET_FD fd, uint32_t events, IOCallback cb);
    /*
     * this API is thread-safe
     */
    KMError updateFd(SOCKET_FD fd, uint32_t events);
    /*
     * this API is thread-safe
     */
    KMError unregisterFd(SOCKET_FD fd, bool close_fd);
    
    PollType getPollType() const;
    bool isPollLT() const; // level trigger
    
public:
    bool inSameThread() const;
    
    /* create a token, it can be used to cancel the tasks that are scheduled with it
     * if caller can guarantee the resources used by tasks are valid when task running,
     * then token is no need, otherwise the caller should cancel the tasks queued in loop
     * before the resources are unavailable
     */
    Token createToken();
    
    /* run the task in loop thread and wait untill task is executed.
     * the task will be executed at once if called on loop thread
     * token is always unnecessary for sync task
     *
     * @param f the task to be executed. it will always be executed when call success
     *
     * @return return the result of f()
     */
    template<typename Callable>
    auto invoke(Callable &&f)
    {
        if (inSameThread()) {
            return f();
        }
        KMError err;
        return Invoker<decltype(f()), Callable>::sync(std::forward<Callable>(f), this, err);
    }

    /* run the task in loop thread and wait untill task is executed.
     * the task will be executed at once if called on loop thread
     * token is always unnecessary for sync task
     *
     * @param f the task to be executed. it will always be executed when call success
     * @param err when f is executed, err is KMError::NOERR, otherwise is KMError
     *
     * @return return the result of f()
     */
    template<typename Callable>
    auto invoke(Callable &&f, KMError &err)
    {
        if (inSameThread()) {
            return f();
        }
        return Invoker<decltype(f()), Callable>::sync(std::forward<Callable>(f), this, err);
    }

    /* run the task in loop thread and wait untill task is executed.
     * the task will be executed at once if called on loop thread
     * token is always unnecessary for sync task
     *
     * @param task the task to be executed. it will always be executed when call success
     */
    KMError sync(Task task);
    
    /* run the task in loop thread.
     * the task will be executed at once if called on loop thread
     *
     * @param task the task to be executed. it will always be executed when call success
     * @param token to be used to cancel the task. If token is null, the caller should
     *              make sure the resources referenced by task are valid when task running
     */
    KMError async(Task task, Token *token=nullptr);
    
    /* run the task in loop thread at next time.
     *
     * @param task the task to be executed. it will always be executed when call success
     * @param token to be used to cancel the task. If token is null, the caller should
     *              make sure the resources referenced by task are valid when task running
     */
    KMError post(Task task, Token *token=nullptr);
    
    /* cancel the tasks that are scheduled with token. you cannot cancel the task that is in running,
     * but will wait untill the task completion
     *
     * @param token token of the tasks
     * this API is thread-safe
     */
    void cancel(Token *token);
    
    void loopOnce(uint32_t max_wait_ms);
    void loop(uint32_t max_wait_ms = -1);
    void stop();
    
    class Impl;
    Impl* pimpl();

protected:
    template <typename ReturnType, typename Callable>
    struct Invoker
    {
        template<typename LoopType>
        static ReturnType sync(Callable &&f, LoopType *loop, KMError &err) {
            ReturnType retval;
            auto task_sync = [&] { retval = f(); };
            err = loop->sync(std::move(task_sync));
            return retval;
        }
    };

    template <typename Callable>
    struct Invoker<void, Callable>
    {
        template<typename LoopType>
        static void sync(Callable &&f, LoopType *loop, KMError &err) {
            err = loop->sync(std::forward<Callable>(f));
        }
    };

private:
    Impl* pimpl_;
};

class KUMA_API TcpSocket
{
public:
    using EventCallback = std::function<void(KMError)>;
    
    TcpSocket(EventLoop *loop);
    ~TcpSocket();
    
    /**
     * Set SSL flags. only the flags set before connect will take effect
     */
    KMError setSslFlags(uint32_t ssl_flags);
    uint32_t getSslFlags() const;
    bool sslEnabled() const;
    KMError setSslServerName(const char *server_name);
    KMError bind(const char *bind_host, uint16_t bind_port);
    KMError connect(const char *host, uint16_t port, EventCallback cb, uint32_t timeout_ms = 0);
    KMError attachFd(SOCKET_FD fd);
    KMError detachFd(SOCKET_FD &fd);
    KMError startSslHandshake(SslRole ssl_role, EventCallback cb);
    KMError getAlpnSelected(char *buf, size_t len);
    int send(const void *data, size_t length);
    int send(const iovec *iovs, int count);
    int send(const KMBuffer &buf);
    int receive(void *data, size_t length);
    
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
    
    TcpListener(EventLoop *loop);
    ~TcpListener();
    
    KMError startListen(const char *host, uint16_t port);
    KMError stopListen(const char *host, uint16_t port);
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
    
    UdpSocket(EventLoop *loop);
    ~UdpSocket();
    
    KMError bind(const char *bind_host, uint16_t bind_port, uint32_t udp_flags=0);
    int send(const void *data, size_t length, const char *host, uint16_t port);
    int send(const iovec *iovs, int count, const char *host, uint16_t port);
    int send(const KMBuffer &buf, const char *host, uint16_t port);
    int receive(void *data, size_t length, char *ip_buf, size_t ip_len, uint16_t &port);
    
    KMError close();
    
    KMError mcastJoin(const char *mcast_addr, uint16_t mcast_port);
    KMError mcastLeave(const char *mcast_addr, uint16_t mcast_port);
    
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
    
    Timer(EventLoop *loop);
    ~Timer();
    
    /**
     * Schedule the timer. This API is thread-safe
     */
    bool schedule(uint32_t delay_ms, TimerMode mode, TimerCallback cb);
    
    /**
     * Cancel the scheduled timer. This API is thread-safe
     */
    void cancel();
    
    class Impl;
    Impl* pimpl();
    
private:
    Impl* pimpl_;
};

class KUMA_API HttpParser
{
public:
    using DataCallback = std::function<void(KMBuffer &)>;
    using EventCallback = std::function<void(HttpEvent)>;
    /**
     * enumerate callback, return true to continue, return false to stop enumerate
     */
    using EnumerateCallback = std::function<bool(const char*, const char*)>; // (name, value)
    
    HttpParser();
    ~HttpParser();
    
    // return bytes parsed
    int parse(const char *data, size_t len);
    int parse(const KMBuffer &buf);
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
    bool isUpgradeTo(const char *protocol) const;
    
    int getStatusCode() const;
    const char* getUrl() const;
    const char* getUrlPath() const;
    const char* getUrlQuery() const;
    const char* getMethod() const;
    const char* getVersion() const;
    const char* getParamValue(const char *name) const;
    const char* getHeaderValue(const char *name) const;
    
    void forEachParam(const EnumerateCallback &cb) const;
    void forEachHeader(const EnumerateCallback &cb) const;
    
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
    using DataCallback = std::function<void(KMBuffer &)>;
    using EventCallback = std::function<void(KMError)>;
    using HttpEventCallback = std::function<void(void)>;
    using EnumerateCallback = HttpParser::EnumerateCallback;
    
    /* 
     * @param ver, http version, "HTTP/2.0" for HTTP2
     */
    HttpRequest(EventLoop *loop, const char *ver = "HTTP/1.1");
    ~HttpRequest();
    
    KMError setSslFlags(uint32_t ssl_flags);
    
    /* set proxy server info and credentials
     * @param proxy_url proxy server info, for example, http://proxy.example.com:3128
     * @param domain_user domain and user name, domain\username or username
     * @param passwd proxy password
     */
    KMError setProxyInfo(const char *proxy_url, const char *domain_user, const char *passwd);
    KMError addHeader(const char *name, const char *value);
    KMError addHeader(const char *name, uint32_t value);
    KMError sendRequest(const char *method, const char *url);
    int sendData(const void *data, size_t len);
    int sendData(const KMBuffer &buf);
    void reset(); // reset for connection reuse
    
    KMError close();
    
    int getStatusCode() const;
    const char* getVersion() const;
    const char* getHeaderValue(const char *name) const;
    void forEachHeader(const EnumerateCallback &cb) const;
    
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
    using DataCallback = std::function<void(KMBuffer &)>;
    using EventCallback = std::function<void(KMError)>;
    using HttpEventCallback = std::function<void(void)>;
    using EnumerateCallback = HttpParser::EnumerateCallback;
    
    /*
     * @param ver, http version, "HTTP/2.0" for HTTP2
     */
    HttpResponse(EventLoop *loop, const char *ver);
    ~HttpResponse();
    
    KMError setSslFlags(uint32_t ssl_flags);
    KMError attachFd(SOCKET_FD fd, const KMBuffer *init_buf=nullptr);
    KMError attachSocket(TcpSocket &&tcp, HttpParser &&parser, const KMBuffer *init_buf=nullptr);
    KMError attachStream(uint32_t stream_id, H2Connection *conn);
    KMError addHeader(const char *name, const char *value);
    KMError addHeader(const char *name, uint32_t value);
    KMError sendResponse(int status_code, const char *desc = nullptr);
    int sendData(const void *data, size_t len);
    int sendData(const KMBuffer &buf);
    void reset(); // reset for connection reuse
    
    KMError close();
    
    const char* getMethod() const;
    const char* getPath() const;
    const char* getQuery() const;
    const char* getVersion() const;
    const char* getParamValue(const char *name) const;
    const char* getHeaderValue(const char *name) const;
    void forEachHeader(const EnumerateCallback &cb) const;
    
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
    using DataCallback = std::function<void(KMBuffer &, bool/*is_text*/, bool/*is_fin*/)>;
    using EventCallback = std::function<void(KMError)>;
    /**
     * HandshakeCallback is called when server received client opening handshake
     * user can check the request in this callback, and return false to reject the handshake
     */
    using HandshakeCallback = std::function<bool(KMError)>;
    using EnumerateCallback = HttpParser::EnumerateCallback;
    
    /*
     * @param ver, http version, "HTTP/2.0" for HTTP2, "HTTP/1.1"
     */
    WebSocket(EventLoop *loop, const char *http_ver = "HTTP/1.1");
    ~WebSocket();
    
    KMError setSslFlags(uint32_t ssl_flags);
    void setOrigin(const char *origin);
    const char* getOrigin() const;
    
    /**
     * add subprotocol to handshake request/response
     */
    KMError setSubprotocol(const char *subprotocol);
    const char* getSubprotocol() const;
    const char* getExtensions() const;
    
    /* set proxy server info and credentials
     * @param proxy_url proxy server info, for example, http://proxy.example.com:3128
     * @param domain_user domain and user name, domain\username or username
     * @param passwd proxy password
     */
    KMError setProxyInfo(const char *proxy_url, const char *domain_user, const char *passwd);
    
    /**
     * add user defined headers
     */
    KMError addHeader(const char *name, const char *value);
    KMError addHeader(const char *name, uint32_t value);
    
    KMError connect(const char *ws_url);
    KMError attachFd(SOCKET_FD fd, const KMBuffer *init_buf, HandshakeCallback cb);
    KMError attachSocket(TcpSocket &&tcp, HttpParser &&parser, const KMBuffer *init_buf, HandshakeCallback cb);
    KMError attachStream(uint32_t stream_id, H2Connection *conn, HandshakeCallback cb);
    
    /**
     * @param flags, bit 1 -- no compression flag when PMCE is negotiated
     */
    int send(const void *data, size_t len, bool is_text, bool is_fin=true, uint32_t flags=0);
    int send(const KMBuffer &buf, bool is_text, bool is_fin=true, uint32_t flags=0);
    
    KMError close();
    
    const char* getPath() const;
    const char* getHeaderValue(const char *name) const;
    void forEachHeader(const EnumerateCallback &cb) const;
    
    void setOpenCallback(EventCallback cb);
    void setDataCallback(DataCallback cb);
    void setWriteCallback(EventCallback cb);
    void setErrorCallback(EventCallback cb);
    
    class Impl;
    Impl* pimpl();
    
private:
    Impl* pimpl_;
};

class KUMA_API ProxyConnection
{
public:
    using EventCallback = std::function<void(KMError)>;
    using DataCallback = std::function<KMError(uint8_t*, size_t)>;
    
    ProxyConnection(EventLoop *loop);
    ~ProxyConnection();
    
    KMError setSslFlags(uint32_t ssl_flags);
    uint32_t getSslFlags() const;
    bool sslEnabled() const;
    KMError setSslServerName(const char *server_name);
    
    /* set proxy server info and credentials
     * @param proxy_url proxy server info, for example, http://proxy.example.com:3128
     * @param domain_user domain and user name, domain\username or username
     * @param passwd proxy password
     */
    KMError setProxyInfo(const char *proxy_url, const char *domain_user, const char *passwd);
    KMError connect(const char *host, uint16_t port, EventCallback cb);
    int send(const void* data, size_t len);
    int send(const iovec* iovs, int count);
    int send(const KMBuffer &buf);
    KMError close();
    
    void setDataCallback(DataCallback cb);
    void setWriteCallback(EventCallback cb);
    void setErrorCallback(EventCallback cb);
    
    bool canSendData() const;
    bool sendBufferEmpty() const;
    
    class Impl;
    Impl* pimpl();
    
private:
    Impl* pimpl_;
};

class KUMA_API H2Connection
{
public:
    // (Stream ID, Method, Path, Origin, Protocol)
    using AcceptCallback = std::function<bool(uint32_t, const char*, const char*, const char*, const char*)>;
    using ErrorCallback = std::function<void(int)>;
    
    H2Connection(EventLoop *loop);
    ~H2Connection();
    
    KMError setSslFlags(uint32_t ssl_flags);
    KMError attachFd(SOCKET_FD fd, const KMBuffer *init_buf=nullptr);
    KMError attachSocket(TcpSocket &&tcp, HttpParser &&parser, const KMBuffer *init_buf=nullptr);
    
    KMError close();
    
    void setAcceptCallback(AcceptCallback cb);
    void setErrorCallback(ErrorCallback cb);
    
    class Impl;
    Impl* pimpl();
    
private:
    Impl* pimpl_;
};

using TraceFunc = std::function<void(int, const char*)>; // (level, msg)

KUMA_API void init(const char *path = nullptr);
KUMA_API void fini();
KUMA_API void setTraceFunc(TraceFunc func);

KUMA_NS_END

#endif
