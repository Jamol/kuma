/* Copyright (c) 2017, Fengping Bao <jamol@live.com>
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

#include "DnsResolver.h"
#include "libkev/src/utils/utils.h"
#include "libkev/src/utils/kmtrace.h"

#if defined(KUMA_OS_WIN)
# include <Ws2tcpip.h>
# include <windows.h>
#elif defined(KUMA_OS_LINUX)
# include <string.h>
# include <sys/socket.h>
# include <netdb.h>
#elif defined(KUMA_OS_MAC)
# include <string.h>
# include <sys/socket.h>
# include <netdb.h>
#else
# error "UNSUPPORTED OS"
#endif

#include <atomic>
#include <chrono>
using namespace std::chrono;

using namespace kuma;

KUMA_NS_BEGIN

constexpr int record_expires_ms = 10000; // 10 senonds
static std::string toEAIString(int v);

using addrinfo_ptr = std::shared_ptr<struct addrinfo>;

class DnsRecord
{
public:
    DnsRecord() = default;
    DnsRecord(const DnsRecord &rhs)
    : host(rhs.host), addr_list(rhs.addr_list), time(rhs.time)
    {
        
    }
    DnsRecord(DnsRecord &&rhs)
    : host(std::move(rhs.host)), addr_list(std::move(rhs.addr_list)), time(std::move(rhs.time))
    {
        
    }
    DnsRecord& operator=(const DnsRecord &rhs)
    {
        if (&rhs != this) {
            host = rhs.host;
            time = rhs.time;
            addr_list = rhs.addr_list;
        }
        return *this;
    }
    DnsRecord& operator=(DnsRecord &&rhs)
    {
        if (&rhs != this) {
            host = std::move(rhs.host);
            time = std::move(rhs.time);
            addr_list = std::move(rhs.addr_list);
        }
        return *this;
    }
    
    std::string host;
    std::vector<sockaddr_storage> addr_list;
    time_point<steady_clock> time;
};
static LockType s_records_locker;
static std::map<std::string, DnsRecord> s_dns_records;

KUMA_NS_END

DnsResolver::DnsResolver()
{
    
}

DnsResolver::~DnsResolver()
{
    if (!stop_flag_) {
        stop();
    }
}

DnsResolver& DnsResolver::get()
{
    static DnsResolver s_instance;
    static std::once_flag s_once_flag;
    std::call_once(s_once_flag, [] {
        s_instance.init();
    });
    return s_instance;
}

KMError DnsResolver::resolve(const std::string &host, sockaddr_storage &addr)
{
    if (getAddress(host, addr) == KMError::NOERR) {
        return KMError::NOERR;
    }
    return doResolve(host, addr);
}

void DnsResolver::cancel(const std::string &host, const Token &t)
{
    auto slot  = t.lock();
    if (slot) {
        slot->cancel();
    }
}

bool DnsResolver::init()
{
    stop_flag_ = false;
    for (int i=0; i<thread_count_; ++i) {
        auto thr = std::thread([=]{
            dnsProc();
        });
        threads_.push_back(std::move(thr));
    }
    return true;
}

void DnsResolver::stop()
{
    {
        LockGuard g(locker_);
        stop_flag_ = true;
    }
    conv_.notify_all();
    
    for (auto &thr : threads_) {
        if(thr.joinable()) {
            try {
                thr.join();
            } catch (...) {
                KM_INFOTRACE("failed to join DNS resolving thread");
            }
        }
    }
    threads_.clear();
}

void DnsResolver::dnsProc()
{
    while (!stop_flag_) {
        std::string host;
        SlotList slots;
        
        {
            std::unique_lock<LockType> lk(locker_);
            conv_.wait(lk, [this] { return !requests_.empty() || stop_flag_; });
            if (stop_flag_) {
                break;
            }
            auto it = requests_.begin();
            if (it != requests_.end()) {
                host = std::move(it->first);
                slots.swap(it->second);
                requests_.erase(it);
            }
        }
        
        if (host.empty()) {
            continue;
        }

        sockaddr_storage addr = { 0 };
        auto ret = doResolve(host, addr);
        for (auto &slot : slots) {
            if (slot) {
                (*slot)(ret, addr);
            }
        }
    }
    
    KM_INFOTRACE("DNS resolving thread exited");
}

KMError DnsResolver::doResolve(const std::string &host, sockaddr_storage &addr)
{
    struct addrinfo hints = { 0 };
    hints.ai_family = AF_UNSPEC;
    hints.ai_flags = AI_ADDRCONFIG; // will block 10 seconds in some case if not set AI_ADDRCONFIG
    struct addrinfo* ai = nullptr;
    auto ret = getaddrinfo(host.c_str(), nullptr, &hints, &ai);
    if(ret != 0 || !ai) {
        KM_ERRTRACE("DNS resolving failure, host=" << host << ", err=" << toEAIString(ret));
        if(ai) freeaddrinfo(ai);
        return KMError::FAILED;
    }
    std::vector<sockaddr_storage> addr_list;
    for (auto *rp = ai; rp; rp = rp->ai_next) {
        sockaddr_storage ss_addr;
        size_t sz = (std::min)(sizeof(ss_addr), (size_t)rp->ai_addrlen);
        memcpy(&ss_addr, rp->ai_addr, sz);
        addr_list.emplace_back(ss_addr);
    }
    freeaddrinfo(ai);
    if (addr_list.empty()) {
        return KMError::FAILED;
    }
    addr = addr_list[0];
    char ip[128] = { 0 };
    kev::km_get_sock_addr((struct sockaddr*)&addr, sizeof(addr), ip, sizeof(ip), nullptr);
    KM_INFOTRACE("DNS resolved, host=" << host << ", ip=" << ip << ", sz=" << addr_list.size());
    DnsRecord dr;
    dr.host = host;
    dr.time = steady_clock::now();
    dr.addr_list = std::move(addr_list);
    addRecord(std::move(dr));
    return KMError::NOERR;
}

void DnsResolver::addRecord(const DnsRecord &dr)
{
    LockGuard g(s_records_locker);
    s_dns_records[dr.host] = dr;
}

void DnsResolver::addRecord(DnsRecord &&dr)
{
    LockGuard g(s_records_locker);
    s_dns_records[dr.host] = std::move(dr);
}

KMError DnsResolver::getAddress(const std::string &host, sockaddr_storage &addr)
{
    LockGuard g(s_records_locker);
    auto it = s_dns_records.find(host);
    if (it != s_dns_records.end()) {
        auto current = steady_clock::now();
        auto diff_ms = duration_cast<milliseconds>(current - it->second.time).count();
        if (diff_ms < record_expires_ms && !it->second.addr_list.empty()) {
            addr = it->second.addr_list[0];
            return KMError::NOERR;
        }
        s_dns_records.erase(it);
    }
    return KMError::NOT_EXIST;
}

KMError DnsResolver::getAddresses(const std::string &host, std::vector<sockaddr_storage> &addr_list)
{
    LockGuard g(s_records_locker);
    auto it = s_dns_records.find(host);
    if (it != s_dns_records.end()) {
        auto current = steady_clock::now();
        auto diff_ms = duration_cast<milliseconds>(current - it->second.time).count();
        if (diff_ms < record_expires_ms && !it->second.addr_list.empty()) {
            addr_list = it->second.addr_list;
            return KMError::NOERR;
        }
        s_dns_records.erase(it);
    }
    return KMError::NOT_EXIST;
}

KUMA_NS_BEGIN
static std::string toEAIString(int v)
{
    switch (v) {
#ifndef KUMA_OS_WIN
        case EAI_ADDRFAMILY:
            return "EAI_ADDRFAMILY";
#endif
        case EAI_AGAIN:
            return "EAI_AGAIN";
        case EAI_BADFLAGS:
            return "EAI_BADFLAGS";
        case EAI_FAIL:
            return "EAI_FAIL";
        case EAI_FAMILY:
            return "EAI_FAMILY";
        case EAI_MEMORY:
            return "EAI_MEMORY";
#ifndef KUMA_OS_WIN
        case EAI_NODATA:
            return "EAI_NODATA";
#endif
        case EAI_NONAME:
            return "EAI_NONAME";
        case EAI_SERVICE:
            return "EAI_SERVICE";
        case EAI_SOCKTYPE:
            return "EAI_SOCKTYPE";
#ifndef KUMA_OS_WIN
        case EAI_SYSTEM:
            return "EAI_SYSTEM";
#endif
        default:
            return std::to_string(v);
    }
}
KUMA_NS_END
