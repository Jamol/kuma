//
//  hpack.cpp
//  kuma
//
//  Created by Jamol Bao on 6/24/16.
//  Copyright © 2016 Jamol. All rights reserved.
//

#include "hpack.h"
#include <math.h>

KUMA_NS_BEGIN

int encodeInteger(uint8_t N, uint64_t I, uint8_t *buf, size_t len) {
    uint8_t *ptr = buf;
    const uint8_t *end = buf + len;
    if (ptr == end) {
        return -1;
    }
    uint8_t NF = (1 << N) - 1;
    if (I < NF) {
        *ptr &= NF ^ 0xFF;
        *ptr |= I;
        return 1;
    }
    *ptr++ |= NF;
    I -= NF;
    while (ptr < end && I >= 128) {
        *ptr++ = I % 128 + 128;
        I /= 128;
    }
    if (ptr == end) {
        return -1;
    }
    *ptr++ = I;
    
    return int(ptr - buf);
}

int encodeString(const std::string &str, uint8_t *buf, size_t len) {
    uint8_t *ptr = buf;
    uint8_t *end = buf + len;
    bool H = false;
    *ptr = H ? 0x80 : 0;
    int ret = encodeInteger(7, str.length(), ptr, end - ptr);
    if (ret <= 0) {
        return -1;
    }
    ptr += ret;
    if (end - ptr < str.length()) {
        return -1;
    }
    memcpy(ptr, str.c_str(), str.length());
    ptr += str.length();
    
    return int(ptr - buf);
}

int decodeInteger(uint8_t N, const uint8_t *buf, size_t len, uint64_t &I) {
    if (N > 8) {
        return -1;
    }
    const uint8_t *ptr = buf;
    const uint8_t *end = buf + len;
    if (ptr == end) {
        return -1;
    }
    uint8_t NF = (1 << N) - 1;
    uint8_t prefix = (*ptr++) & NF;
    if (prefix < NF) {
        I = prefix;
        return 1;
    }
    if (ptr == end) {
        return -1;
    }
    int m = 0;
    uint64_t u64 = prefix;
    uint8_t b = 0;
    do {
        b = *ptr++;
        u64 += (b & 127) * pow(2, m);
        m += 7;
    } while (ptr < end && (b & 128));
    if (ptr == end && (b & 128)) {
        return -1;
    }
    I = u64;
    
    return int(ptr - buf);
}

int decodeString(const uint8_t *buf, size_t len, std::string &str)
{
    const uint8_t *ptr = buf;
    const uint8_t *end = buf + len;
    if (ptr == end) {
        return -1;
    }
    uint8_t NF = 0x7F;
    bool H = *ptr & NF;
    uint64_t str_len = 0;
    int ret = decodeInteger(7, ptr, end - ptr, str_len);
    if (ret <= 0) {
        return -1;
    }
    ptr += ret;
    if ( str_len > end - ptr) {
        return -1;
    }
    str.assign((const char*)ptr, str_len);
    ptr += str_len;
    
    return int(ptr - buf);
}

enum class PrefixType {
    INDEXED_HEADER,
    LITERAL_HEADER_WITH_INDEXING,
    LITERAL_HEADER_WITHOUT_INDEXING,
    TABLE_SIZE_UPDATE
};

int decodePrefix(const uint8_t *buf, size_t len, PrefixType &type, uint64_t &I) {
    const uint8_t *ptr = buf;
    const uint8_t *end = buf + len;
    uint8_t N = 0;
    if (*ptr & 0x80) {
        N = 7;
        type = PrefixType::INDEXED_HEADER;
    } else if (*ptr & 0x40) {
        N = 6;
        type = PrefixType::LITERAL_HEADER_WITH_INDEXING;
    } else if (*ptr & 0x20) {
        N = 5;
        type = PrefixType::TABLE_SIZE_UPDATE;
    } else {
        N = 4;
        type = PrefixType::LITERAL_HEADER_WITHOUT_INDEXING;
    }
    int ret = decodeInteger(N, ptr, end - ptr, I);
    if (ret <= 0) {
        return -1;
    }
    ptr += ret;
    return int(ptr - buf);
}

#define TABLE_ENTRY_SIZE_EXTRA 32
#define HPACK_DYNAMIC_START_INDEX 62
#define HPACK_STATIC_TABLE_SIZE 61
static KeyValuePair hpackStaticTable[HPACK_STATIC_TABLE_SIZE] = {
    std::make_pair(":authority", ""),
    std::make_pair(":method", "GET"),
    std::make_pair(":method", "POST"),
    std::make_pair(":path", "/"),
    std::make_pair(":path", "/index.html"),
    
    std::make_pair(":scheme", "http"),
    std::make_pair(":scheme", "https"),
    std::make_pair(":status", "200"),
    std::make_pair(":status", "204"),
    std::make_pair(":status", "206"),
    
    std::make_pair(":status", "304"),
    std::make_pair(":status", "400"),
    std::make_pair(":status", "404"),
    std::make_pair(":status", "500"),
    std::make_pair("accept-charset", ""),
    
    std::make_pair("accept-encoding", "gzip, deflate"),
    std::make_pair("accept-language", ""),
    std::make_pair("accept-ranges", ""),
    std::make_pair("accept", ""),
    std::make_pair("access-control-allow-origin", ""),
    
    std::make_pair("age", ""),
    std::make_pair("allow", ""),
    std::make_pair("authorization", ""),
    std::make_pair("cache-control", ""),
    std::make_pair("content-disposition", ""),
    
    std::make_pair("content-encoding", ""),
    std::make_pair("content-language", ""),
    std::make_pair("content-length", ""),
    std::make_pair("content-location", ""),
    std::make_pair("content-range", ""),
    
    std::make_pair("content-type", ""),
    std::make_pair("cookie", ""),
    std::make_pair("date", ""),
    std::make_pair("etag", ""),
    std::make_pair("expect", ""),
    
    std::make_pair("expires", ""),
    std::make_pair("from", ""),
    std::make_pair("host", ""),
    std::make_pair("if-match", ""),
    std::make_pair("if-modified-since", ""),
    
    std::make_pair("if-none-match", ""),
    std::make_pair("if-range", ""),
    std::make_pair("if-unmodified-since", ""),
    std::make_pair("last-modified", ""),
    std::make_pair("link", ""),
    
    std::make_pair("location", ""),
    std::make_pair("max-forwards", ""),
    std::make_pair("proxy-authenticate", ""),
    std::make_pair("proxy-authorization", ""),
    std::make_pair("range", ""),
    
    std::make_pair("referer", ""),
    std::make_pair("refresh", ""),
    std::make_pair("retry-after", ""),
    std::make_pair("server", ""),
    std::make_pair("set-cookie", ""),
    
    std::make_pair("strict-transport-security", ""),
    std::make_pair("transfer-encoding", ""),
    std::make_pair("user-agent", ""),
    std::make_pair("vary", ""),
    std::make_pair("via", ""),
    
    std::make_pair("www-authenticate", "")
};

HPacker::HPacker() {
    for (int i = 0; i < HPACK_STATIC_TABLE_SIZE; ++i) {
        indexMap_.emplace(hpackStaticTable[i].first, std::make_pair(-1 , i));
    }
}

bool HPacker::getIndexedName(int index, std::string &name) {
    if (index <= 0) {
        return false;
    }
    if (index < HPACK_DYNAMIC_START_INDEX) {
        name = hpackStaticTable[index - 1].first;
    } else if (index - HPACK_DYNAMIC_START_INDEX < dynamicTable_.size()) {
        name = dynamicTable_[index - HPACK_DYNAMIC_START_INDEX].first;
    } else {
        return false;
    }
    return true;
}

bool HPacker::getIndexedValue(int index, std::string &value)
{
    if (index <= 0) {
        return false;
    }
    if (index < HPACK_DYNAMIC_START_INDEX) {
        value = hpackStaticTable[index - 1].second;
    } else if (index - HPACK_DYNAMIC_START_INDEX < dynamicTable_.size()) {
        value = dynamicTable_[index - HPACK_DYNAMIC_START_INDEX].second;
    } else {
        return false;
    }
    return true;
}

bool HPacker::addHeaderToTable(const std::string &name, const std::string &value)
{
    uint32_t entrySize = uint32_t(name.length() + value.length() + TABLE_ENTRY_SIZE_EXTRA);
    if (entrySize + tableSize_ > tableSizeLimit_) {
        evictTableBySize(entrySize + tableSize_ - tableSizeLimit_);
    }
    if (entrySize > tableSizeLimit_) {
        return false;
    }
    dynamicTable_.push_front(std::make_pair(name, value));
    tableSize_ += entrySize;
    return true;
}

void HPacker::updateTableLimit(size_t limit)
{
    if (tableSize_ > limit) {
        evictTableBySize(tableSize_ - limit);
    }
    tableSizeLimit_ = limit;
}

void HPacker::evictTableBySize(size_t size)
{
    uint32_t evicted = 0;
    while (evicted < size && !dynamicTable_.empty()) {
        auto &entry = dynamicTable_.back();
        uint32_t entrySize = uint32_t(entry.first.length() + entry.second.length() + TABLE_ENTRY_SIZE_EXTRA);
        tableSize_ -= tableSize_ > entrySize ? entrySize : tableSize_;
        if (isEncoder) {
            auto it = indexMap_.find(entry.first);
            if (it != indexMap_.end() && it->second.first == dynamicTable_.size() - 1) {
                it->second.first = -1; // reset dynamic table index
            }
        }
        dynamicTable_.pop_back();
        evicted += entrySize;
    }
}

HPacker::IndexingType HPacker::getIndexingType(const std::string &name)
{
    if (name == "cookie") {
        return IndexingType::ALL;
    }
    return IndexingType::NONE;
}

int HPacker::encodeSizeUpdate(int sz, uint8_t *buf, size_t len)
{
    uint8_t *ptr = buf;
    const uint8_t *end = buf + len;
    *ptr &= 0x20;
    int ret = encodeInteger(5, sz, ptr, end - ptr);
    if (ret <= 0) {
        return -1;
    }
    ptr += ret;
    return int(ptr - buf);
}

int HPacker::encodeHeader(const std::string &name, const std::string &value, uint8_t *buf, size_t len)
{
    uint8_t *ptr = buf;
    const uint8_t *end = buf + len;
    int index = -1;
    bool valueIndexed = false;
    auto it = indexMap_.find(name);
    if (it != indexMap_.end()) {
        int indexD = it->second.first;
        int indexS = it->second.second;
        if (indexD != -1 && indexD < dynamicTable_.size() && name == dynamicTable_[indexD].first) {
            index = indexD + HPACK_DYNAMIC_START_INDEX;
            valueIndexed = dynamicTable_[indexD].second == value;
        } else if (indexS != -1 && indexS < HPACK_STATIC_TABLE_SIZE && name == hpackStaticTable[indexS].first) {
            index = indexS + 1;
            valueIndexed = hpackStaticTable[indexS].second == value;
        }
    }
    bool addToTable = false;
    if (index != -1) {
        if (valueIndexed) { // name and value indexed
            *ptr &= 0x80;
            int ret = encodeInteger(7, index, ptr, end - ptr);
            if (ret <= 0) {
                return -1;
            }
            ptr += ret;
        } else { // name indexed
            IndexingType idxType = getIndexingType(name);
            uint8_t N = 0;
            if (idxType == IndexingType::ALL) {
                *ptr &= 0x40;
                N = 6;
                addToTable = true;
            } else {
                *ptr &= 0x00;
                N = 4;
            }
            int ret = encodeInteger(N, index, ptr, end - ptr);
            if (ret <= 0) {
                return -1;
            }
            ptr += ret;
            ret = encodeString(value, ptr, end - ptr);
            if (ret <= 0) {
                return -1;
            }
            ptr += ret;
        }
    } else {
        IndexingType idxType = getIndexingType(name);
        if (idxType == IndexingType::ALL) {
            *ptr++ = 0x40;
            addToTable = true;
        } else {
            *ptr++ = 0x00;
        }
        int ret = encodeString(name, ptr, end - ptr);
        if (ret <= 0) {
            return -1;
        }
        ptr += ret;
        ret = encodeString(value, ptr, end - ptr);
        if (ret <= 0) {
            return -1;
        }
        ptr += ret;
    }
    if (addToTable) {
        addHeaderToTable(name, value);
    }
    return int(ptr - buf);
}

int HPacker::encode(KeyValueVector headers, uint8_t *buf, size_t len) {
    isEncoder = true;
    uint8_t *ptr = buf;
    const uint8_t *end = buf + len;
    
    if (updateTableSize) {
        updateTableSize = false;
        updateTableLimit(tableSizeMax_);
        int ret = encodeSizeUpdate(int(tableSizeLimit_), ptr, end - ptr);
        if (ret <= 0) {
            return -1;
        }
        ptr += ret;
    }
    for (auto &hdr : headers) {
        int ret = encodeHeader(hdr.first, hdr.second, ptr, end - ptr);
        if (ret <= 0) {
            return -1;
        }
        ptr += ret;
    }
    return int(ptr - buf);
}

int HPacker::decode(const uint8_t *buf, size_t len) {
    isEncoder = false;
    const uint8_t *ptr = buf;
    const uint8_t *end = buf + len;
    
    while (ptr < end) {
        std::string name;
        std::string value;
        PrefixType type;
        uint64_t I = 0;
        int ret = decodePrefix(ptr, end - ptr, type, I);
        if (ret <= 0) {
            return -1;
        }
        ptr += ret;
        if (PrefixType::INDEXED_HEADER == type) {
            if (!getIndexedName(int(I), name) || !getIndexedValue(int(I), value)) {
                return -1;
            }
        } else if (PrefixType::LITERAL_HEADER_WITH_INDEXING == type ||
                   PrefixType::LITERAL_HEADER_WITHOUT_INDEXING == type) {
            if (0 == I) {
                ret = decodeString(ptr, end - ptr, name);
                if (ret <= 0) {
                    return -1;
                }
                ptr += ret;
            } else if (!getIndexedName(int(I), name)) {
                return -1;
            }
            ret = decodeString(ptr, end - ptr, value);
            if (ret <= 0) {
                return -1;
            }
            ptr += ret;
            if (PrefixType::LITERAL_HEADER_WITH_INDEXING == type) {
                addHeaderToTable(name, value);
            }
        } else if (PrefixType::TABLE_SIZE_UPDATE == type) {
            if (I > tableSizeMax_) {
                return -1;
            }
            updateTableLimit(I);
        }
    }
    return int(len);
}

KUMA_NS_END
