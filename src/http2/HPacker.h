/* Copyright (c) 2016, Fengping Bao <jamol@live.com>
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

#ifndef __HPacker_H__
#define __HPacker_H__

#include "kmdefs.h"
#include <string>
#include <deque>
#include <map>
#include <vector>

#include "HPackTable.h"

KUMA_NS_BEGIN

class HPacker
{
public:
    using KeyValuePair = HPackTable::KeyValuePair;
    using KeyValueVector = std::vector<KeyValuePair>;
    
public:
    HPacker();
    
    int encode(KeyValueVector headers, uint8_t *buf, size_t len);
    int decode(const uint8_t *buf, size_t len, KeyValueVector &headers);
    void setMaxTableSize(size_t maxSize) { table_.setMaxSize(maxSize); }
    
private:
    int encodeHeader(const std::string &name, const std::string &value, uint8_t *buf, size_t len);
    int encodeSizeUpdate(int sz, uint8_t *buf, size_t len);

    enum class IndexingType {
        NONE,
        NAME,
        ALL
    };
    IndexingType getIndexingType(const std::string &name);
    
private:
    HPackTable table_;
    bool updateTableSize_ = true;
};

KUMA_NS_END

#endif /* __HPACK_H__ */
