// cache.hpp — cache contract + in-memory store (Laravel: Illuminate\Contracts\Cache).
//
// get/put/has/forget are the virtual surface; put with a TTL expires entries; remember()
// and get_or() are non-virtual sugar. Header-only.
#pragma once
#include <chrono>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

class CacheContract {
public:
    virtual ~CacheContract() = default;
    virtual void put(const std::string& key, const std::string& value) = 0;
    virtual void put(const std::string& key, const std::string& value,
                     std::chrono::milliseconds ttl) = 0;
    virtual std::optional<std::string> get(const std::string& key) const = 0;
    virtual bool has(const std::string& key) const = 0;
    virtual void forget(const std::string& key) = 0;

    std::string get_or(const std::string& key, const std::string& def) const {
        auto v = get(key);
        return v ? *v : def;
    }

    // Return the cached value, or compute + store + return it.
    std::string remember(const std::string& key, const std::function<std::string()>& compute) {
        auto v = get(key);
        if (v) return *v;
        std::string value = compute();
        put(key, value);
        return value;
    }
};

// In-memory cache (Laravel: the "array" driver), with optional per-key expiry.
class TaggedCache; // fluent tag view, defined below

class ArrayCache : public CacheContract {
public:
    // Thread-safe (shared across HTTP worker threads). NOTE: a get-then-put done by a
    // caller (e.g. the throttle counter) is still a non-atomic read-modify-write, so
    // counts are approximate under heavy concurrency — fine for a dev rate limiter.
    void put(const std::string& key, const std::string& value) override {
        std::lock_guard<std::mutex> lock(mutex_);
        items_[key] = {value, std::nullopt};
    }
    void put(const std::string& key, const std::string& value,
             std::chrono::milliseconds ttl) override {
        std::lock_guard<std::mutex> lock(mutex_);
        items_[key] = {value, Clock::now() + ttl};
    }
    std::optional<std::string> get(const std::string& key) const override {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = items_.find(key);
        if (it == items_.end()) return std::nullopt;
        if (expired(it->second)) {
            items_.erase(it);
            return std::nullopt;
        }
        return it->second.value;
    }
    bool has(const std::string& key) const override { return get(key).has_value(); }
    void forget(const std::string& key) override {
        std::lock_guard<std::mutex> lock(mutex_);
        items_.erase(key);
    }

    // Tag-based invalidation: Cache::tags({"posts"}).put(...) / .flush().
    TaggedCache tags(std::vector<std::string> tag_list);

private:
    friend class TaggedCache;
    using Clock = std::chrono::steady_clock;
    struct Entry {
        std::string value;
        std::optional<Clock::time_point> expires;
    };
    static bool expired(const Entry& e) { return e.expires && Clock::now() >= *e.expires; }

    mutable std::unordered_map<std::string, Entry> items_; // mutable: get() purges expired
    std::unordered_map<std::string, std::vector<std::string>> tagged_keys_; // tag -> keys
    mutable std::mutex mutex_;
};

// A view of an ArrayCache scoped to one or more tags. flush() forgets every key
// stored under any of those tags.
class TaggedCache {
public:
    TaggedCache(ArrayCache& cache, std::vector<std::string> tag_list)
        : cache_(&cache), tags_(std::move(tag_list)) {}

    void put(const std::string& key, const std::string& value) {
        cache_->put(key, value);
        for (const auto& t : tags_) cache_->tagged_keys_[t].push_back(key);
    }
    std::optional<std::string> get(const std::string& key) const { return cache_->get(key); }

    void flush() {
        for (const auto& t : tags_) {
            auto it = cache_->tagged_keys_.find(t);
            if (it == cache_->tagged_keys_.end()) continue;
            for (const auto& key : it->second) cache_->forget(key);
            cache_->tagged_keys_.erase(it);
        }
    }

private:
    ArrayCache* cache_;
    std::vector<std::string> tags_;
};

inline TaggedCache ArrayCache::tags(std::vector<std::string> tag_list) {
    return TaggedCache(*this, std::move(tag_list));
}
