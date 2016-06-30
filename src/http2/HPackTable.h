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

#ifndef __HPackTable_H__
#define __HPackTable_H__

#include "kmdefs.h"
#include <string>
#include <deque>
#include <map>

KUMA_NS_BEGIN

class HPackTable {
public:
    using KeyValuePair = std::pair<std::string, std::string>;
    using KeyValueQueue = std::deque<KeyValuePair>;
    using IndexMap = std::map<std::string, std::pair<int, int>>;
    
public:
    HPackTable();
    void setMode(bool isEncoder) { isEncoder_ = isEncoder; }
    void setMaxSize(size_t maxSize) { maxSize_ = maxSize; }
    void updateLimitSize(size_t limitSize);
    int  getIndex(const std::string &name, const std::string &value, bool &valueIndexed);
    bool getIndexedName(int index, std::string &name);
    bool getIndexedValue(int index, std::string &value);
    bool addHeader(const std::string &name, const std::string &value);
    
    size_t getMaxSize() { return maxSize_; }
    size_t getLimitSize() { return limitSize_; }
    size_t getTableSize() { return tableSize_; }
    
private:
    int getDynamicIndex(int idxSeq);
    void updateIndex(const std::string &name, int idxSeq);
    void removeIndex(const std::string &name);
    bool getIndex(const std::string &name, int &indexD, int &indexS);
    void evictTableBySize(size_t size);
    
private:
    KeyValueQueue dynamicTable_;
    size_t tableSize_ = 0;
    size_t limitSize_ = 4096;
    size_t maxSize_ = 4096;
    
    bool isEncoder_ = false;
    int indexSequence_ = 0;
    IndexMap indexMap_; // <header name, <dynamic index sequence, static index>>
};

KUMA_NS_END

#endif /* __HPackTable_H__ */
