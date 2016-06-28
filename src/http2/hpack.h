//
//  hpack.hpp
//  kuma
//
//  Created by Jamol Bao on 6/24/16.
//  Copyright © 2016 Jamol. All rights reserved.
//

#ifndef __HPACK_H__
#define __HPACK_H__

#include "kmdefs.h"
#include <deque>
#include <map>

KUMA_NS_BEGIN

using KeyValuePair = std::pair<std::string, std::string>;
using KeyValueQueue = std::deque<KeyValuePair>;
using KeyValueVector = std::deque<KeyValuePair>;
using IndexMap = std::map<std::string, std::pair<int, int>>;

class HPacker
{
public:
    HPacker();
    
    int encode(KeyValueVector headers, uint8_t *buf, size_t len);
    int decode(const uint8_t *buf, size_t len);
    
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
    
private:
    KeyValueQueue dynamicTable_;
    size_t tableSize_ = 0;
    size_t tableSizeLimit_ = 4096;
    size_t tableSizeMax_ = 4096;
    
    bool updateTableSize = true;
    bool isEncoder = true;
    IndexMap indexMap_; // <header name, <dynamic index, static index>>
};

KUMA_NS_END

#endif /* __HPACK_H__ */
