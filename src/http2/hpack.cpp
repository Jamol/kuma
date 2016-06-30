/* Copyright (c) 2014-2016, Fengping Bao <jamol@live.com>
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

#include "hpack.h"
#include "hpack_huffman_table.h"

#include <math.h>
#include <vector>

KUMA_NS_BEGIN

#include "StaticTable.h"

static char *huffDecodeBits(char *dst, uint8_t bits, uint8_t *state, bool *ending) {
    const auto &entry = huff_decode_table[*state][bits];
    
    if ((entry.flags & NGHTTP2_HUFF_FAIL) != 0)
        return nullptr;
    if ((entry.flags & NGHTTP2_HUFF_SYM) != 0)
        *dst++ = entry.sym;
    *state = entry.state;
    *ending = (entry.flags & NGHTTP2_HUFF_ACCEPTED) != 0;
    
    return dst;
}

static int huffDecode(const uint8_t *src, size_t len, std::string &str) {
    uint8_t state = 0;
    bool ending = false;
    const uint8_t *src_end = src + len;

    std::vector<char> str_buf;
    str_buf.resize(2*len);
    char *ptr = &str_buf[0];
    
    for (; src != src_end; ++src) {
        if ((ptr = huffDecodeBits(ptr, *src >> 4, &state, &ending)) == nullptr)
            return -1;
        if ((ptr = huffDecodeBits(ptr, *src & 0xf, &state, &ending)) == nullptr)
            return -1;
    }
    if (!ending) {
        return -1;
    }
    int str_len = int(ptr - &str_buf[0]);
    str.assign(&str_buf[0], str_len);
    return str_len;
}
static int huffEncode(const std::string &str, uint8_t *buf, size_t len) {
    uint8_t *ptr = buf;
    const uint8_t *end = buf + len;
    const char* src = str.c_str();
    const char* src_end = src + str.length();
    
    uint64_t current = 0;
    uint32_t n = 0;
    
    for (; src != src_end;) {
        const auto &sym = huff_sym_table[*src++];
        uint32_t code = sym.code;
        uint32_t nbits = sym.nbits;
        
        current <<= nbits;
        current |= code;
        n += nbits;
        
        while (n >= 8) {
            n -= 8;
            *ptr++ = current >> n;
        }
    }
    
    if (n > 0) {
        current <<= (8 - n);
        current |= (0xFF >> n);
        *ptr++ = current;
    }
    
    return int(ptr - buf);
}

static int encodeInteger(uint8_t N, uint64_t I, uint8_t *buf, size_t len) {
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

static int encodeString(const std::string &str, bool H, uint8_t *buf, size_t len) {
    uint8_t *ptr = buf;
    uint8_t *end = buf + len;
    *ptr = H ? 0x80 : 0;
    int ret = encodeInteger(7, str.length(), ptr, end - ptr);
    if (ret <= 0) {
        return -1;
    }
    ptr += ret;
    if (H) {
        int ret = huffEncode(str, ptr, end - ptr);
        if (ret < 0) {
            return -1;
        }
        ptr += ret;
    } else {
        if (end - ptr < str.length()) {
            return -1;
        }
        memcpy(ptr, str.c_str(), str.length());
        ptr += str.length();
    }
    
    return int(ptr - buf);
}

static int decodeInteger(uint8_t N, const uint8_t *buf, size_t len, uint64_t &I) {
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

static int decodeString(const uint8_t *buf, size_t len, std::string &str)
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
    if (H) {
        if(huffDecode(ptr, str_len, str) < 0) {
            return -1;
        }
    } else {
        str.assign((const char*)ptr, str_len);
    }
    ptr += str_len;
    
    return int(ptr - buf);
}

enum class PrefixType {
    INDEXED_HEADER,
    LITERAL_HEADER_WITH_INDEXING,
    LITERAL_HEADER_WITHOUT_INDEXING,
    TABLE_SIZE_UPDATE
};

static int decodePrefix(const uint8_t *buf, size_t len, PrefixType &type, uint64_t &I) {
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

HPacker::HPacker() {
    for (int i = 0; i < HPACK_STATIC_TABLE_SIZE; ++i) {
        indexMap_.emplace(hpackStaticTable[i].first, std::make_pair(-1, i));
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
    if (isEncoder_) {
        updateIndex(name, ++indexSequence_);
    }
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
        if (isEncoder_) {
            removeIndex(entry.first);
        }
        dynamicTable_.pop_back();
        evicted += entrySize;
    }
}

int HPacker::getDynamicIndex(int idxSeq)
{
    return -1 == idxSeq ? -1 : indexSequence_ - idxSeq;
}

void HPacker::updateIndex(const std::string &name, int idxSeq)
{
    auto it = indexMap_.find(name);
    if (it != indexMap_.end()) {
        it->second.first = idxSeq;
    } else {
        indexMap_.emplace(name, std::make_pair(idxSeq, -1));
    }
}

void HPacker::removeIndex(const std::string &name)
{
    auto it = indexMap_.find(name);
    if (it != indexMap_.end()) {
        int idx = getDynamicIndex(it->second.first);
        if (idx == dynamicTable_.size() - 1) {
            if (it->second.second == -1) {
                indexMap_.erase(it);
            } else {
                it->second.first = -1; // reset dynamic table index
            }
        }
    }
}

bool HPacker::getIndex(const std::string &name, int &indexD, int &indexS)
{
    indexD = -1;
    indexS = -1;
    auto it = indexMap_.find(name);
    if (it != indexMap_.end()) {
        indexD = getDynamicIndex(it->second.first);
        indexS = it->second.second;
        return true;
    }
    return false;
}

int HPacker::getHPackIndex(const std::string &name, const std::string &value, bool &valueIndexed)
{
    int index = -1, indexD = -1, indexS = -1;
    valueIndexed = false;
    getIndex(name, indexD, indexS);
    if (indexD != -1 && indexD < dynamicTable_.size() && name == dynamicTable_[indexD].first) {
        index = indexD + HPACK_DYNAMIC_START_INDEX;
        valueIndexed = dynamicTable_[indexD].second == value;
    } else if (indexS != -1 && indexS < HPACK_STATIC_TABLE_SIZE && name == hpackStaticTable[indexS].first) {
        index = indexS + 1;
        valueIndexed = hpackStaticTable[indexS].second == value;
    }
    return index;
}

HPacker::IndexingType HPacker::getIndexingType(const std::string &name)
{
    if (name == "cookie" || name == ":authority" || name == "user-agent" || name == "pragma") {
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
    
    bool valueIndexed = false;
    int index = getHPackIndex(name, value, valueIndexed);
    bool addToTable = false;
    if (index != -1) {
        uint8_t N = 0;
        if (valueIndexed) { // name and value indexed
            *ptr = 0x80;
            N = 7;
        } else { // name indexed
            IndexingType idxType = getIndexingType(name);
            if (idxType == IndexingType::ALL) {
                *ptr = 0x40;
                N = 6;
                addToTable = true;
            } else {
                *ptr = 0x10;
                N = 4;
            }
        }
        // encode prefix Bits
        int ret = encodeInteger(N, index, ptr, end - ptr);
        if (ret <= 0) {
            return -1;
        }
        ptr += ret;
        if (!valueIndexed) {
            ret = encodeString(value, true, ptr, end - ptr);
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
            *ptr++ = 0x10;
        }
        int ret = encodeString(name, true, ptr, end - ptr);
        if (ret <= 0) {
            return -1;
        }
        ptr += ret;
        ret = encodeString(value, true, ptr, end - ptr);
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
    isEncoder_ = true;
    uint8_t *ptr = buf;
    const uint8_t *end = buf + len;
    
    if (updateTableSize_) {
        updateTableSize_ = false;
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

int HPacker::decode(const uint8_t *buf, size_t len, KeyValueVector &headers) {
    isEncoder_ = false;
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
        headers.emplace_back(std::make_pair(name, value));
    }
    return int(len);
}

KUMA_NS_END
