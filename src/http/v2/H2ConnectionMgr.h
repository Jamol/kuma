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

#ifndef __H2ConnectionMgr_H__
#define __H2ConnectionMgr_H__

#include "kmdefs.h"
#include <memory>
#include <mutex>

#include "h2defs.h"
#include "H2ConnectionImpl.h"

KUMA_NS_BEGIN

class H2ConnectionMgr
{
public:
	H2ConnectionMgr() = default;
	~H2ConnectionMgr() = default;
    
    void addConnection(const std::string &key, H2ConnectionPtr &conn);
    void addConnection(const std::string &key, H2ConnectionPtr &&conn);
    H2ConnectionPtr getConnection(const std::string &key);
    H2ConnectionPtr getConnection(const std::string &host, uint16_t port, uint32_t ssl_flags, const EventLoopPtr &loop);
    void removeConnection(const std::string key);
    
public:
    static H2ConnectionMgr& getRequestConnMgr(bool secure) { return secure ? reqSecureConnMgr_ : reqConnMgr_;}
    static H2ConnectionMgr reqConnMgr_;
    static H2ConnectionMgr reqSecureConnMgr_;

private:
    using H2ConnectionMap = std::map<std::string, H2ConnectionPtr>;
    H2ConnectionMap connMap_;
    std::mutex connMutex_;
};

KUMA_NS_END

#endif
