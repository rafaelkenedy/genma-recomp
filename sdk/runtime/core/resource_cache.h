#pragma once
#include <condition_variable>
#include <list>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace cache {
// Immutable, byte-bounded resources. Concurrent requests for one key share a
// load, while unrelated keys can load independently. Active callers own leases.
template <class Key, class Value, class Hash = std::hash<Key>> class Resources {
    struct Entry {
        std::condition_variable ready;
        bool loading = true;
        std::shared_ptr<const Value> value;
        typename std::list<Key>::iterator position;
    };
    std::mutex mutex_;
    std::unordered_map<Key, std::shared_ptr<Entry>, Hash> entries_;
    std::list<Key> lru_;
    size_t budget_;

  public:
    struct Stats {
        size_t bytes = 0, hits = 0, misses = 0, evictions = 0;
    };

  private:
    Stats stats_;

  public:
    explicit Resources(size_t budget) : budget_(budget) {}
    template <class Loader> std::shared_ptr<const Value> Get(const Key& key, Loader&& load, bool* hit = nullptr) {
        std::shared_ptr<Entry> entry;
        {
            std::unique_lock lock(mutex_);
            auto found = entries_.find(key);
            if (found != entries_.end()) {
                entry = found->second;
                entry->ready.wait(lock, [&] { return !entry->loading; });
                ++stats_.hits;
                if (hit)
                    *hit = true;
                // Oversized or failed loads are shared but not retained.
                if (auto current = entries_.find(key); current != entries_.end() && current->second == entry)
                    lru_.splice(lru_.begin(), lru_, entry->position);
                return entry->value;
            }
            entry = std::make_shared<Entry>();
            entries_.emplace(key, entry);
            ++stats_.misses;
            if (hit)
                *hit = false;
        }
        std::shared_ptr<const Value> value;
        try {
            value = load();
        } catch (...) {
            {
                std::lock_guard lock(mutex_);
                entry->loading = false;
                entries_.erase(key);
            }
            entry->ready.notify_all();
            throw;
        }
        std::vector<std::shared_ptr<Entry>> retired;
        {
            std::lock_guard lock(mutex_);
            entry->value = value;
            entry->loading = false;
            size_t bytes = value ? value->CacheBytes() : 0;
            if (!value || !bytes || bytes > budget_)
                entries_.erase(key);
            else {
                while (!lru_.empty() && stats_.bytes > budget_ - bytes) {
                    auto oldest = entries_.find(lru_.back());
                    stats_.bytes -= oldest->second->value->CacheBytes();
                    retired.push_back(std::move(oldest->second));
                    entries_.erase(oldest);
                    lru_.pop_back();
                    ++stats_.evictions;
                }
                lru_.push_front(key);
                entry->position = lru_.begin();
                stats_.bytes += bytes;
            }
        }
        entry->ready.notify_all();
        return value; // retired resources are released outside the cache mutex
    }
    Stats Snapshot() {
        std::lock_guard lock(mutex_);
        return stats_;
    }
};
} // namespace cache
