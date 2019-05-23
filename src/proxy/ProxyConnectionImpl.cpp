/* Copyright (c) 2014-2019, Fengping Bao <jamol@live.com>
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

#include "ProxyConnectionImpl.h"
#include "util/kmtrace.h"
#include "http/Uri.h"


#if defined(KUMA_OS_WIN)
# include <windows.h>
#elif defined(KUMA_OS_LINUX)

#elif defined(KUMA_OS_MAC)

#else
# error "UNSUPPORTED OS"
#endif

using namespace kuma;

ProxyConnection::Impl::Impl(const EventLoopPtr &loop)
: TcpConnection(loop)
{
    
}

ProxyConnection::Impl::~Impl()
{
    
}

KMError ProxyConnection::Impl::setProxyInfo(const ProxyInfo &proxy_info)
{
    KUMA_INFOTRACE("ProxyConnection::setProxyInfo, proxy=" << proxy_info.url);
    Uri uri;
    if (!uri.parse(proxy_info.url)) {
        return KMError::INVALID_PARAM;
    }
    proxy_info_ = proxy_info;
    proxy_addr_ = uri.getHost();
    proxy_port_ = static_cast<uint16_t>(std::stoi(uri.getPort()));
    return KMError::NOERR;
}

KMError ProxyConnection::Impl::connect(const std::string &host, uint16_t port, EventCallback cb)
{
    if (proxy_addr_.empty()) {
        TcpConnection::setDataCallback(proxy_data_cb_);
        TcpConnection::setWriteCallback(proxy_write_cb_);
        TcpConnection::setErrorCallback(proxy_error_cb_);
        
        return TcpConnection::connect(host, port, std::move(cb));
    } else {
        TcpConnection::setDataCallback([this](uint8_t *data, size_t size) {
            return onTcpData(data, size);
        });
        TcpConnection::setWriteCallback([this] (KMError) {
            onTcpWrite();
        });
        TcpConnection::setErrorCallback([this] (KMError err) {
            onTcpError(err);
        });
        
        http_parser_.reset();
        http_parser_.setDataCallback([this] (KMBuffer &buf) { onHttpData(buf); });
        http_parser_.setEventCallback([this] (HttpEvent ev) { onHttpEvent(ev); });
        
        connect_host_ = host;
        connect_port_ = port;
        proxy_connect_cb_ = std::move(cb);
        if (TcpConnection::sslEnabled()) {
            proxy_ssl_flags_ = getSslFlags();
            TcpConnection::setSslFlags(0);
        }
        
        num_of_attempts_ = 0;
        setState(State::CONNECTING);
        return TcpConnection::connect(proxy_addr_, proxy_port_, [this] (KMError err) {
            onTcpConnect(err);
        });
    }
}

KMError ProxyConnection::Impl::attachFd(SOCKET_FD fd, const KMBuffer *init_buf)
{
    TcpConnection::setDataCallback(proxy_data_cb_);
    TcpConnection::setWriteCallback(proxy_write_cb_);
    TcpConnection::setErrorCallback(proxy_error_cb_);
    
    return TcpConnection::attachFd(fd, init_buf);
}

KMError ProxyConnection::Impl::attachSocket(TcpSocket::Impl &&tcp, const KMBuffer *init_buf)
{
    TcpConnection::setDataCallback(proxy_data_cb_);
    TcpConnection::setWriteCallback(proxy_write_cb_);
    TcpConnection::setErrorCallback(proxy_error_cb_);
    
    return TcpConnection::attachSocket(std::move(tcp), init_buf);
}

KMError ProxyConnection::Impl::sendProxyRequest()
{
    http_parser_.reset();
    http_parser_.setRequestMethod("CONNECT");
    setState(State::AUTHENTICATING);

    auto req = buildProxyRequest();
    auto ret = TcpConnection::send(req.c_str(), req.size());
    if (ret < 0) {
        return KMError::FAILED;
    }
    return KMError::NOERR;
}

std::string ProxyConnection::Impl::buildProxyRequest()
{
    std::stringstream ss;
    ss << "CONNECT " << connect_host_ << ":" << connect_port_ << " HTTP/1.1\r\n";
    ss << "User-Agent: " << UserAgent << "\r\n";
    if (!host_.empty()) {
        ss << "Host: " << connect_host_ << ":" << connect_port_ << "\r\n";
    }
    ss << "Content-Length: 0" << "\r\n";
    ss << "Proxy-Connection: Keep-Alive" << "\r\n";
    ss << "Pragma: no-cache" << "\r\n";
    if (proxy_auth_ && proxy_auth_->hasAuthHeader()) {
        auto auth_header = proxy_auth_->getAuthHeader();
        if (!auth_header.empty()) {
            ss << strProxyAuthorization << ": " << auth_header << "\r\n";
        }
    }
    ss << "\r\n";
    return ss.str();
}

KMError ProxyConnection::Impl::handleProxyResponse()
{
    KUMA_INFOTRACE("ProxyConnection::handleProxyResponse, code=" << http_parser_.getStatusCode());
    if (http_parser_.getStatusCode() == 407) {
        if (num_of_attempts_ >= 5) {
            onProxyError(KMError::NOT_AUTHORIZED);
            return KMError::FAILED;
        }
        auto str_conn = http_parser_.getHeaderValue("Connection");
        if (str_conn.empty()) {
            str_conn = http_parser_.getHeaderValue(strProxyConnection);
        }
        bool need_reconnect = is_equal(str_conn, "Close");
        std::string scheme, challenge;
        http_parser_.forEachHeader([&scheme, &challenge] (const std::string &name, const std::string &value) {
            if (is_equal(name, strProxyAuthenticate)) {
                const auto pos = value.find(' ');
                const auto s = value.substr(0, pos);
                if (is_equal(s, "NTLM") ||
                    is_equal(s, "Negotiate") ||
                    is_equal(s, "Digest") ||
                    is_equal(s, "Basic")) {
                    scheme = std::move(s);
                    if (pos != std::string::npos) {
                        challenge = value.substr(pos + 1);
                    }
                    return false;
                }
            }
            return true;
        });
        if (scheme.empty()) {
            KUMA_ERRTRACE("ProxyConnection::handleProxyResponse, auth scheme is empty");
            onProxyError(KMError::FAILED);
            return KMError::FAILED;
        }
        KUMA_INFOTRACE("ProxyConnection::handleProxyResponse, token: \"" << scheme << " " << challenge << "\"");
        if (!proxy_auth_) {
            auto auth_scheme = ProxyAuthenticator::getAuthScheme(scheme);
            proxy_auth_ = ProxyAuthenticator::create(
                {auth_scheme, proxy_info_.user, proxy_info_.passwd},
                {proxy_addr_, proxy_port_, "CONNECT", "/", "HTTP"}
            );
            if (!proxy_auth_) {
                onProxyError(KMError::NOT_AUTHORIZED);
                return KMError::FAILED;
            }
        }
        proxy_auth_->nextAuthToken(challenge);
        ++num_of_attempts_;
        
        if (need_reconnect) {
            TcpConnection::close();
            
            setState(State::CONNECTING);
            auto ret = TcpConnection::connect(proxy_addr_, proxy_port_, [this] (KMError err) {
                onTcpConnect(err);
            });
            if (ret != KMError::NOERR) {
                onProxyConnect(ret);
            }
        } else {
            sendProxyRequest();
        }
    } else if (http_parser_.getStatusCode() == 200) {
#ifdef KUMA_HAS_OPENSSL
        if (proxy_ssl_flags_ != 0) {
            setState(State::SSL_CONNECTING);
            TcpConnection::setSslFlags(proxy_ssl_flags_);
            proxy_ssl_flags_ = 0;
            TcpConnection::startSslHandshake(SslRole::CLIENT, [this] (KMError err) {
                onProxyConnect(err);
            });
        } else
#endif
        {
            onProxyConnect(KMError::NOERR);
        }
    }
    return KMError::NOERR;
}

void ProxyConnection::Impl::onTcpConnect(KMError err)
{
    if (err == KMError::NOERR) {
        auto ret = sendProxyRequest();
        if (ret != KMError::NOERR) {
            onProxyConnect(KMError::FAILED);
        }
    } else {
        onProxyConnect(err);
    }
}

KMError ProxyConnection::Impl::onTcpData(uint8_t *data, size_t size)
{
    if (getState() == State::OPEN) {
        onProxyData(data, size);
    } else if (getState() == State::AUTHENTICATING) {
        int bytes_used = http_parser_.parse((char*)data, size);
    }
    return KMError::NOERR;
}

void ProxyConnection::Impl::onTcpWrite()
{
    onProxyWrite();
}

void ProxyConnection::Impl::onTcpError(KMError err)
{
    onProxyError(err);
}

void ProxyConnection::Impl::onProxyConnect(KMError err)
{
    if (err == KMError::NOERR) {
        setState(State::OPEN);
    }
    if (proxy_connect_cb_) {
        proxy_connect_cb_(err);
    }
}

void ProxyConnection::Impl::onProxyData(uint8_t *data, size_t size)
{
    if (proxy_data_cb_) {
        proxy_data_cb_(data, size);
    }
}

void ProxyConnection::Impl::onProxyWrite()
{
    if (getState() == State::OPEN && proxy_write_cb_) {
        proxy_write_cb_(KMError::NOERR);
    }
}

void ProxyConnection::Impl::onProxyError(KMError err)
{
    if (proxy_error_cb_) {
        proxy_error_cb_(err);
    }
}

void ProxyConnection::Impl::onHttpData(KMBuffer &buf)
{
    
}

void ProxyConnection::Impl::onHttpEvent(HttpEvent ev)
{
    KUMA_INFOTRACE("ProxyConnection::onHttpEvent, ev="<<int(ev));
    switch (ev) {
        case HttpEvent::HEADER_COMPLETE:
            break;
            
        case HttpEvent::COMPLETE:
            handleProxyResponse();
            break;
            
        case HttpEvent::HTTP_ERROR:
            break;
            
        default:
            break;
    }
}
