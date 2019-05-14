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

#include "ProxyConnection.h"
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
    TcpConnection::setDataCallback([this](uint8_t *data, size_t size) {
        return onTcpData(data, size);
    });
    TcpConnection::setWriteCallback([this] (KMError) {
        onTcpWrite();
    });
    TcpConnection::setErrorCallback([this] (KMError err) {
        onTcpError(err);
    });
    
    http_parser_.setDataCallback([this] (KMBuffer &buf) { onHttpData(buf); });
    http_parser_.setEventCallback([this] (HttpEvent ev) { onHttpEvent(ev); });
}

ProxyConnection::Impl::~Impl()
{
    
}

KMError ProxyConnection::Impl::setProxyInfo(const std::string &proxy_url, const std::string &user, const std::string &passwd)
{
    Uri uri;
    if (!uri.parse(proxy_url)) {
        return KMError::INVALID_PARAM;
    }
    proxy_addr_ = uri.getHost();
    proxy_port_ = static_cast<uint16_t>(std::stoi(uri.getPort()));
    proxy_user_ = user;
    proxy_passwd_ = passwd;
    return KMError::NOERR;
}

KMError ProxyConnection::Impl::connect(const std::string &host, uint16_t port, EventCallback cb)
{
    connect_host_ = host;
    connect_port_ = port;
    proxy_connect_cb_ = std::move(cb);
    
    setState(State::CONNECTING);
    return TcpConnection::connect(proxy_addr_, proxy_port_, [this] (KMError err) {
        onTcpConnect(err);
    });
}

KMError ProxyConnection::Impl::sendProxyRequest()
{
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
    if (http_parser_.getStatusCode() == 407) {
        auto str_conn = http_parser_.getHeaderValue("Connection");
        if (str_conn.empty()) {
            str_conn = http_parser_.getHeaderValue(strProxyConnection);
        }
        bool need_reconnect = is_equal(str_conn, "Close");
        std::string scheme, chellage;
        http_parser_.forEachHeader([&scheme, &chellage] (const std::string &name, const std::string &value) {
            if (is_equal(name, strProxyAuthenticate)) {
                const auto pos = value.find(' ');
                const auto s = value.substr(0, pos);
                if (is_equal(s, "NTLM") ||
                    is_equal(s, "Negotiate") ||
                    is_equal(s, "Basic")) {
                    scheme = std::move(s);
                    if (pos != std::string::npos) {
                        chellage = value.substr(pos + 1);
                    }
                    return false;
                }
            }
            return true;
        });
        if (scheme.empty()) {
            return KMError::FAILED;
        }
        if (!proxy_auth_) {
            proxy_auth_ = ProxyAuthenticator::create(scheme, proxy_user_, proxy_passwd_);
            if (!proxy_auth_) {
                return KMError::FAILED;
            }
        }
        proxy_auth_->nextAuthToken(chellage);
        
        if (need_reconnect) {
            TcpConnection::close();
            http_parser_.reset();
            
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
        onProxyConnect(KMError::NOERR);
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
    KUMA_INFOTRACE("onHttpEvent, ev="<<int(ev));
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
