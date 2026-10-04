#pragma once

#include "base.h"

#include <unordered_map>
#include <deque>

template <typename T>
class Handle_Table {
public:
    u32 Add(T value) {
        if (nextHandle == UINT32_MAX) {
            return 0;
        }

        usize index;
        if (freed.empty()) {
            index = entries.size();
            entries.emplace_back();
        }
        else {
            index = freed.back();
            freed.pop_back();
        }
        
        Entry& entry = entries[index];
        entry.value = std::move(value);
        entry.handle = ++nextHandle;
        handles.emplace(entry.handle, index);
        return entry.handle;
    }

    T* Find(u32 handle) {
        auto found = handles.find(handle);
        return found != handles.end() ? &entries[found->second].value : nullptr;
    }

    T& At(u32 index) {
        return entries[index].value;
    }

    u32 Handle(u32 index) const {
        return entries[index].handle;
    }

    u32 Index(u32 handle) const {
        auto found = handles.find(handle);
        return found != handles.end() ? static_cast<u32>(found->second) : UINT32_MAX;
    }

    void Remove(u32 index) {
        Entry& entry = entries[index];
        if (entry.handle == 0) {
            return;
        }
        handles.erase(entry.handle);
        entry.value = T();
        entry.handle = 0;
        freed.push_back(index);
    }

    template <typename Each>
    void For_Each(Each each) {
        for (usize index = 0; index < entries.size(); index++) {
            if (entries[index].handle != 0) {
                each(static_cast<u32>(index), entries[index].value);
            }
        }
    }

    void Clear() {
        entries.clear();
        freed.clear();
        handles.clear();
    }

private:
    struct Entry {
        T value = {};
        u32 handle = 0;
    };

    u32 nextHandle = 0;
    std::deque<Entry> entries;
    std::vector<usize> freed;
    std::unordered_map<u32, usize> handles;
};
