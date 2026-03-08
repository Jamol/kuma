/* Copyright (c) 2024, Fengping Bao <jamol@live.com>
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

#include "HebConnector.h"
#include "libkev/src/utils/utils.h"
#include "libkev/src/utils/kmtrace.h"

using namespace kuma;

//////////////////////////////////////////////////////////////////////////
HebConnector::HebConnector(const EventLoopPtr &loop)
: loop_(loop)
{
    KM_SetObjKey("HebConnector");
}

HebConnector::~HebConnector()
{
    stop();
}

KMError HebConnector::start(const std::string &host, uint16_t port, HebConfig config, ConnectCallback cb)
{
    if (connecting_) {
        return KMError::INVALID_STATE;
    }
    
    host_ = host;
    port_ = port;
    connect_cb_ = std::move(cb);
    config_ = config;
    connecting_ = true;
    
    dns_token_ = DnsResolver::get().resolve(host, [this](KMError err, const sockaddr_storage&) {
        auto loop = loop_.lock();
        if (loop) {
            loop->async([this] {
                std::vector<sockaddr_storage> addrs;
                auto err = DnsResolver::get().getAddresses(host_, addrs);
                onResolved(err, addrs);
            });
        }
    });
    
    if (config_.timeout_ms > 0) {
        auto loop = loop_.lock();
        if (loop) {
            timer_ = std::make_unique<Timer::Impl>(loop->getTimerMgr());
            timer_->schedule(config_.timeout_ms, kev::Timer::Mode::ONE_SHOT, [this] {
                onError(KMError::TIMEOUT);
            });
        }
    }
    
    return KMError::NOERR;
}

KMError HebConnector::stop()
{
    cleanup();
    return KMError::NOERR;
}

void HebConnector::onResolved(KMError err, const std::vector<sockaddr_storage>& addrs)
{
    if (!connecting_) {
        return;
    }
    
    if (err != KMError::NOERR || addrs.empty()) {
        onError(err);
        return;
    }

    for (const auto& addr : addrs) {
        if (addr.ss_family == AF_INET6) {
            std::string ip;
            kev::km_get_sock_addr(addr, ip, nullptr);
            if (std::find(ipv6_ips_.begin(), ipv6_ips_.end(), ip) == ipv6_ips_.end()) {
                ipv6_ips_.emplace_back(std::move(ip));
            }
        } else if (addr.ss_family == AF_INET) {
            std::string ip;
            kev::km_get_sock_addr(addr, ip, nullptr);
            if (std::find(ipv4_ips_.begin(), ipv4_ips_.end(), ip) == ipv4_ips_.end()) {
                ipv4_ips_.emplace_back(std::move(ip));
            }
        }
    }

    auto ip = getNextIp();
    if (ip.empty()) {
        onError(KMError::FAILED);
        return;
    }
    connect(ip);
}

std::string HebConnector::getNextIp()
{
    std::string next_ip;
    if (config_.prefer_ipv6) {
        if (!ipv6_ips_.empty()) {
            next_ip = std::move(ipv6_ips_.front());
            ipv6_ips_.erase(ipv6_ips_.begin());
        } else if (!ipv4_ips_.empty()) {
            next_ip = std::move(ipv4_ips_.front());
            ipv4_ips_.erase(ipv4_ips_.begin());
        }
    } else {
        if (!ipv4_ips_.empty()) {
            next_ip = std::move(ipv4_ips_.front());
            ipv4_ips_.erase(ipv4_ips_.begin());
        } else if (!ipv6_ips_.empty()) {
            next_ip = std::move(ipv6_ips_.front());
            ipv6_ips_.erase(ipv6_ips_.begin());
        }
    }
    config_.prefer_ipv6 = !config_.prefer_ipv6;
    return next_ip;
}

void HebConnector::connect(const std::string &ip)
{
    auto socket = SocketBase::create(loop_.lock());
    auto* s = socket.get();
    sockets_[ip] = std::move(socket);
    s->connect(ip, port_, [this, ip](KMError err) {
        onConnect(err, ip);
    });
    if (!next_timer_) {
        auto loop = loop_.lock();
        if (loop) {
            next_timer_ = std::make_unique<Timer::Impl>(loop->getTimerMgr());
        }
    }
    if (next_timer_) {
        next_timer_->schedule(config_.connection_attempt_delay_ms,
                              kev::Timer::Mode::ONE_SHOT, [this] {
            auto ip = getNextIp();
            if (!ip.empty()) {
                connect(ip);
            }
        });
    }
}

void HebConnector::onConnect(KMError err, const std::string &ip)
{
    if (!connecting_) {
        return;
    }
    
    if (err == KMError::NOERR) {
        auto it = sockets_.find(ip);
        if (it != sockets_.end()) {
            auto socket = std::move(it->second);
            sockets_.clear();
            connecting_ = false;
            timer_.reset();
            next_timer_.reset();
            if (connect_cb_) {
                connect_cb_(KMError::NOERR, std::move(socket));
            }
        }
    } else {
        sockets_.erase(ip);
        if (sockets_.empty()) {
            auto ip = getNextIp();
            if (!ip.empty()) {
                connect(ip);
            } else {
                onError(KMError::FAILED);
            }
        }
    }
}

void HebConnector::onError(KMError err)
{
    cleanup();
    if (connect_cb_) {
        connect_cb_(err, {});
    }
}

void HebConnector::cleanup()
{
    connecting_ = false;
    DnsResolver::get().cancel("", dns_token_);
    dns_token_.reset();
    sockets_.clear();
    timer_.reset();
    next_timer_.reset();
}
