/*******************************************************************************
 * Copyright (C) 2020-2021 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#pragma once

#include <list>
#include <stdexcept>
#include <string>
#include <unordered_map>

template <typename Key_T, typename Value_T>
class LRUCache {
  private:
    struct ListNode {
        ListNode(Key_T key, Value_T value) : key(key), value(value) {
        }
        Key_T key;
        Value_T value;
    };
    using ValuesType = typename std::list<ListNode>;
    using KeysValuesType = typename std::unordered_map<Key_T, typename ValuesType::iterator>;

    ValuesType lru_values;
    KeysValuesType keys;
    const size_t MAX_SIZE;

    void insert(Key_T key, Value_T value) {
        auto value_it = lru_values.emplace(lru_values.end(), key, value);
        keys.emplace(key, value_it);
    }

    void make_recently_used(typename ValuesType::iterator value_it) {
        if (std::next(value_it) != lru_values.end()) {
            lru_values.splice(lru_values.end(), lru_values, value_it);
        }
    }

  public:
    LRUCache(size_t size) : MAX_SIZE(size) {
        keys.reserve(MAX_SIZE);
    }

    ~LRUCache() = default;

    Value_T &get(Key_T key) {
        auto key_it = keys.find(key);
        if (key_it == keys.end())
            throw std::runtime_error("Key " + std::to_string(key) + " is absent from LRUCache");

        make_recently_used(key_it->second);
        return key_it->second->value;
    }

    void put(Key_T key, Value_T value = {}) {
        auto key_it = keys.find(key);
        if (key_it == keys.end()) {
            if (keys.size() == MAX_SIZE) {
                keys.erase(lru_values.front().key);
                lru_values.pop_front();
            }
            insert(key, value);
        } else {
            make_recently_used(key_it->second);
            key_it->second->value = value;
        }
    }

    size_t count(const Key_T &key) const {
        return keys.count(key);
    }

    size_t size() const {
        return keys.size();
    }

    void clear() {
        keys.clear();
        lru_values.clear();
    }
};
