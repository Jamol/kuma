/* Copyright (c) 2016, Fengping Bao <jamol@live.com>
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

#include "H2ConnectionMgr.h"
#include "libkev/src/utils/kmtrace.h"
#include "DnsResolver.h"

using namespace kuma;


H2ConnectionMgr H2ConnectionMgr::req_conn_mgr_;
H2ConnectionMgr H2ConnectionMgr::req_secure_conn_mgr_;
//////////////////////////////////////////////////////////////////////////

void H2ConnectionMgr::addConnection(const std::string &key, H2ConnectionPtr &conn)
{
    std::lock_guard<std::mutex> g(conn_mutex_);
    conn_map_[key] = conn;
}

void H2ConnectionMgr::addConnection(const std::string &key, H2ConnectionPtr &&conn)
{
    std::lock_guard<std::mutex> g(conn_mutex_);
    conn_map_[key] = std::move(conn);
}

H2ConnectionPtr H2ConnectionMgr::getConnection(const std::string &key)
{
    std::lock_guard<std::mutex> g(conn_mutex_);
    auto it = conn_map_.find(key);
    return it != conn_map_.end() ? it->second : nullptr;
}

H2ConnectionPtr H2ConnectionMgr::getConnection(const std::string &host, uint16_t port, uint32_t ssl_flags, const EventLoopPtr &loop, const ProxyInfo &proxy_info)
{
    std::string key;
    std::string ip;
    sockaddr_storage ss_addr = { 0 };
    auto ret = DnsResolver::get().resolve(host, port, ss_addr);
    if (ret == KMError::NOERR && kev::km_get_sock_addr(ss_addr, ip, nullptr) == 0) {
        key = ip + ":" + std::to_string(port);
    } else {
        key = host + ":" + std::to_string(port);
    }
    std::lock_guard<std::mutex> g(conn_mutex_);
    auto it = conn_map_.find(key);
    if (it != conn_map_.end()) {
        return it->second;
    }
    H2ConnectionPtr conn(new H2Connection::Impl(loop));
    conn->setConnectionKey(key);
    conn->setSslFlags(ssl_flags);
    conn->setProxyInfo(proxy_info);
    if (conn->connect(host, port) != KMError::NOERR) {
        return H2ConnectionPtr();
    }
    conn_map_[key] = conn;
    return conn;
}

void H2ConnectionMgr::removeConnection(const std::string key)
{
    std::lock_guard<std::mutex> g(conn_mutex_);
    conn_map_.erase(key);
}

void H2ConnectionMgr::removeConnection(const std::string &key, bool secure)
{
    if (!key.empty()) {
        auto &conn_mgr = H2ConnectionMgr::getRequestConnMgr(secure);
        conn_mgr.removeConnection(key);
    }
}
