#pragma once

#include <algorithm>
#include <vector>

// ---------------------------------------------------------------------------
// PointerRegistry<T> — shared add/remove logic for non-owning pointer lists.
//
// Runtime maintains several parallel registries (event receivers, hook
// receivers) that all need the same semantics: dedup on add, no-op on
// remove of unregistered pointers, preserve insertion order for dispatch.
// This class is the single source of truth for that behaviour.
// ---------------------------------------------------------------------------
template<typename T>
class PointerRegistry {
        std::vector<T*> items_;

    public:
        void add(T* item) {
            if (!item) return;
            if (std::find(items_.begin(), items_.end(), item) != items_.end())
                return;
            items_.push_back(item);
        }

        void remove(T* item) {
            std::erase(items_, item);
        }

        void clear() {
            items_.clear();
        }

        const std::vector<T*>& items() const { return items_; }
        std::size_t size() const { return items_.size(); }
        bool empty() const { return items_.empty(); }

        auto begin() const { return items_.begin(); }
        auto end()   const { return items_.end(); }
};
