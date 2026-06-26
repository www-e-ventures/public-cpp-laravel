// session.hpp — session store + handle (Laravel: Illuminate\Session).
//
// A SessionStore keeps per-session-id key/value data; a Session is a handle bound to
// one id with the ergonomic API. ArraySessionStore is the in-memory default. The HTTP
// layer would map a session id to a cookie; here the id is explicit (and testable).
#pragma once
#include <cstdint>
#include <optional>
#include <random>
#include <string>
#include <unordered_map>

// A random opaque session id (hex). The HTTP layer stores this in a cookie.
inline std::string new_session_id() {
    static thread_local std::mt19937_64 rng(std::random_device{}());
    std::uint64_t v = rng();
    static const char* d = "0123456789abcdef";
    std::string out(16, '0');
    for (int i = 15; i >= 0; --i) {
        out[i] = d[v & 0xF];
        v >>= 4;
    }
    return out;
}

class SessionStore {
public:
    virtual ~SessionStore() = default;
    virtual void put(const std::string& sid, const std::string& key, const std::string& value) = 0;
    virtual std::optional<std::string> get(const std::string& sid, const std::string& key) const = 0;
    virtual bool has(const std::string& sid, const std::string& key) const = 0;
    virtual void forget(const std::string& sid, const std::string& key) = 0;
    virtual void flush(const std::string& sid) = 0; // clear the whole session
};

class ArraySessionStore : public SessionStore {
public:
    void put(const std::string& sid, const std::string& key, const std::string& value) override {
        data_[sid][key] = value;
    }
    std::optional<std::string> get(const std::string& sid, const std::string& key) const override {
        auto s = data_.find(sid);
        if (s == data_.end()) return std::nullopt;
        auto k = s->second.find(key);
        if (k == s->second.end()) return std::nullopt;
        return k->second;
    }
    bool has(const std::string& sid, const std::string& key) const override {
        auto s = data_.find(sid);
        return s != data_.end() && s->second.count(key) > 0;
    }
    void forget(const std::string& sid, const std::string& key) override {
        auto s = data_.find(sid);
        if (s != data_.end()) s->second.erase(key);
    }
    void flush(const std::string& sid) override { data_.erase(sid); }

private:
    std::unordered_map<std::string, std::unordered_map<std::string, std::string>> data_;
};

// A handle bound to one session id.
class Session {
public:
    Session(SessionStore& store, std::string id) : store_(&store), id_(std::move(id)) {}

    const std::string& id() const { return id_; }
    void put(const std::string& key, const std::string& value) { store_->put(id_, key, value); }
    std::optional<std::string> get(const std::string& key) const { return store_->get(id_, key); }
    std::string get_or(const std::string& key, const std::string& def) const {
        auto v = get(key);
        return v ? *v : def;
    }
    bool has(const std::string& key) const { return store_->has(id_, key); }
    void forget(const std::string& key) { store_->forget(id_, key); }
    void flush() { store_->flush(id_); }

private:
    SessionStore* store_;
    std::string id_;
};
