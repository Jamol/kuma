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

#include "HPackTable.h"

KUMA_NS_BEGIN

#include "StaticTable.h"

HPackTable::HPackTable() {
    for (int i = 0; i < HPACK_STATIC_TABLE_SIZE; ++i) {
        indexMap_.emplace(hpackStaticTable[i].first, std::make_pair(-1, i));
    }
}

bool HPackTable::getIndexedName(int index, std::string &name) {
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

bool HPackTable::getIndexedValue(int index, std::string &value)
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

bool HPackTable::addHeader(const std::string &name, const std::string &value)
{
    uint32_t entrySize = uint32_t(name.length() + value.length() + TABLE_ENTRY_SIZE_EXTRA);
    if (entrySize + tableSize_ > limitSize_) {
        evictTableBySize(entrySize + tableSize_ - limitSize_);
    }
    if (entrySize > limitSize_) {
        return false;
    }
    dynamicTable_.push_front(std::make_pair(name, value));
    tableSize_ += entrySize;
    if (isEncoder_) {
        updateIndex(name, ++indexSequence_);
    }
    return true;
}

void HPackTable::updateLimitSize(size_t limitSize)
{
    if (tableSize_ > limitSize) {
        evictTableBySize(tableSize_ - limitSize);
    }
    limitSize_ = limitSize;
}

void HPackTable::evictTableBySize(size_t size)
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

int HPackTable::getDynamicIndex(int idxSeq)
{
    return -1 == idxSeq ? -1 : indexSequence_ - idxSeq;
}

void HPackTable::updateIndex(const std::string &name, int idxSeq)
{
    auto it = indexMap_.find(name);
    if (it != indexMap_.end()) {
        it->second.first = idxSeq;
    } else {
        indexMap_.emplace(name, std::make_pair(idxSeq, -1));
    }
}

void HPackTable::removeIndex(const std::string &name)
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

bool HPackTable::getIndex(const std::string &name, int &indexD, int &indexS)
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

int HPackTable::getIndex(const std::string &name, const std::string &value, bool &valueIndexed)
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

KUMA_NS_END
