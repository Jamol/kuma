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

KUMA_NS_BEGIN

class HPacker
{
public:
    using KeyValuePair = std::pair<std::string, std::string>;
    using KeyValueQueue = std::deque<KeyValuePair>;
    using KeyValueVector = std::vector<KeyValuePair>;
    using IndexMap = std::map<std::string, std::pair<int, int>>;
    
public:
    HPacker();
    
    int encode(KeyValueVector headers, uint8_t *buf, size_t len);
    int decode(const uint8_t *buf, size_t len, KeyValueVector &headers);
    void setMaxTableSize(size_t maxSize) { tableSizeMax_ = maxSize; }
    
private:
    int encodeHeader(const std::string &name, const std::string &value, uint8_t *buf, size_t len);
    int encodeSizeUpdate(int sz, uint8_t *buf, size_t len);
    
    bool getIndexedName(int index, std::string &name);
    bool getIndexedValue(int index, std::string &value);
    
    bool addHeaderToTable(const std::string &name, const std::string &value);
    void updateTableLimit(size_t limit);
    void evictTableBySize(size_t size);
    
    enum class IndexingType {
        NONE,
        NAME,
        ALL
    };
    IndexingType getIndexingType(const std::string &name);
    
    int getDynamicIndex(int idxSeq);
    void updateIndex(const std::string &name, int idxSeq);
    void removeIndex(const std::string &name);
    bool getIndex(const std::string &name, int &indexD, int &indexS);
    int getHPackIndex(const std::string &name, const std::string &value, bool &valueIndexed);
    
private:
    KeyValueQueue dynamicTable_;
    size_t tableSize_ = 0;
    size_t tableSizeLimit_ = 4096;
    size_t tableSizeMax_ = 4096;
    
    bool updateTableSize_ = true;
    bool isEncoder_ = true;
    int indexSequence_ = 0;
    IndexMap indexMap_; // <header name, <dynamic index sequence, static index>>
};

KUMA_NS_END

#endif /* __HPACK_H__ */
