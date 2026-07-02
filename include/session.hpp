// session.hpp — session store + handle (Laravel: Illuminate\Session).
//
// A SessionStore keeps per-session-id key/value data; a Session is a handle bound to
// one id with the ergonomic API. ArraySessionStore is the in-memory default. The HTTP
// layer would map a session id to a cookie; here the id is explicit (and testable).
#pragma once
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <fcntl.h>
#include <mutex>
#include <optional>
#include <random>
#include <string>
#include <unistd.h>
#include <unordered_map>

// `count` bytes from the kernel CSPRNG (/dev/urandom), hex-encoded. Session ids and
// CSRF tokens are bearer credentials — they must be unpredictable, which a seeded
// mt19937 stream is not (observe a few outputs and the rest are computable). The
// std::random_device fallback only runs if /dev/urandom can't be opened, which on
// the POSIX systems the socket layer already assumes effectively can't happen.
inline std::string csprng_hex(std::size_t count) {
    static const char* d = "0123456789abcdef";
    std::string bytes(count, '\0');
    int fd = ::open("/dev/urandom", O_RDONLY);
    if (fd >= 0) {
        std::size_t got = 0;
        while (got < count) {
            ssize_t n = ::read(fd, bytes.data() + got, count - got);
            if (n <= 0) break;
            got += static_cast<std::size_t>(n);
        }
        ::close(fd);
        if (got == count) {
            std::string out(count * 2, '0');
            for (std::size_t i = 0; i < count; ++i) {
                unsigned char b = static_cast<unsigned char>(bytes[i]);
                out[i * 2] = d[b >> 4];
                out[i * 2 + 1] = d[b & 0xF];
            }
            return out;
        }
    }
    std::string out;
    out.reserve(count * 2);
    std::random_device rd; // fallback only — not reached on a working POSIX system
    for (std::size_t i = 0; i < count; ++i) {
        unsigned int b = rd() & 0xFF;
        out.push_back(d[b >> 4]);
        out.push_back(d[b & 0xF]);
    }
    return out;
}

// A random opaque session id (32 hex chars / 128 bits, CSPRNG). The HTTP layer
// stores this in a cookie.
inline std::string new_session_id() { return csprng_hex(16); }

class SessionStore {
public:
    virtual ~SessionStore() = default;
    virtual void put(const std::string& sid, const std::string& key, const std::string& value) = 0;
    virtual std::optional<std::string> get(const std::string& sid, const std::string& key) const = 0;
    virtual bool has(const std::string& sid, const std::string& key) const = 0;
    virtual void forget(const std::string& sid, const std::string& key) = 0;
    virtual void flush(const std::string& sid) = 0; // clear the whole session

    // Move a session's data under a new id — the primitive behind
    // Session::regenerate_id() (the session-fixation defense: after login the old,
    // possibly attacker-known id must stop working). Virtual with a default so
    // custom stores keep compiling; the default reports "unsupported" and the
    // session then simply keeps its old id.
    virtual bool rename(const std::string& from, const std::string& to) {
        (void)from;
        (void)to;
        return false;
    }
};

// In-memory store. Thread-safe: one instance is shared across HttpServer worker
// threads. Optional TTL: a session untouched for longer is treated as expired
// (dropped lazily on access, or in bulk by gc()); 0 = sessions never expire.
class ArraySessionStore : public SessionStore {
public:
    explicit ArraySessionStore(std::chrono::milliseconds ttl = std::chrono::milliseconds{0})
        : ttl_(ttl) {}

    void put(const std::string& sid, const std::string& key, const std::string& value) override {
        std::lock_guard<std::mutex> lock(mutex_);
        drop_if_expired(sid);
        data_[sid][key] = value;
        touch(sid);
    }
    std::optional<std::string> get(const std::string& sid, const std::string& key) const override {
        std::lock_guard<std::mutex> lock(mutex_);
        drop_if_expired(sid);
        auto s = data_.find(sid);
        if (s == data_.end()) return std::nullopt;
        auto k = s->second.find(key);
        if (k == s->second.end()) return std::nullopt;
        touch(sid);
        return k->second;
    }
    bool has(const std::string& sid, const std::string& key) const override {
        std::lock_guard<std::mutex> lock(mutex_);
        drop_if_expired(sid);
        auto s = data_.find(sid);
        if (s == data_.end() || s->second.count(key) == 0) return false;
        touch(sid);
        return true;
    }
    void forget(const std::string& sid, const std::string& key) override {
        std::lock_guard<std::mutex> lock(mutex_);
        drop_if_expired(sid);
        auto s = data_.find(sid);
        if (s != data_.end()) s->second.erase(key);
    }
    void flush(const std::string& sid) override {
        std::lock_guard<std::mutex> lock(mutex_);
        data_.erase(sid);
        last_seen_.erase(sid);
    }
    bool rename(const std::string& from, const std::string& to) override {
        std::lock_guard<std::mutex> lock(mutex_);
        drop_if_expired(from);
        auto s = data_.find(from);
        if (s == data_.end()) return false;
        data_[to] = std::move(s->second);
        data_.erase(from);
        last_seen_.erase(from);
        touch(to);
        return true;
    }

    // Purge every expired session at once (e.g. from a periodic task). Lazy
    // per-access expiry already keeps stale ids from answering; this frees memory
    // for sessions that are never touched again.
    void gc() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (ttl_.count() == 0) return;
        auto now = std::chrono::steady_clock::now();
        for (auto it = last_seen_.begin(); it != last_seen_.end();) {
            if (now - it->second > ttl_) {
                data_.erase(it->first);
                it = last_seen_.erase(it);
            } else {
                ++it;
            }
        }
    }

private:
    // Both helpers assume mutex_ is held.
    void touch(const std::string& sid) const {
        if (ttl_.count() != 0) last_seen_[sid] = std::chrono::steady_clock::now();
    }
    void drop_if_expired(const std::string& sid) const {
        if (ttl_.count() == 0) return;
        auto t = last_seen_.find(sid);
        if (t != last_seen_.end() && std::chrono::steady_clock::now() - t->second > ttl_) {
            data_.erase(sid);
            last_seen_.erase(t);
        }
    }

    std::chrono::milliseconds ttl_;
    mutable std::unordered_map<std::string, std::unordered_map<std::string, std::string>> data_;
    mutable std::unordered_map<std::string, std::chrono::steady_clock::time_point> last_seen_;
    mutable std::mutex mutex_;
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

    // Move this session's data under a fresh CSPRNG id and return it — call after a
    // privilege change (login) so a pre-login id an attacker may have planted stops
    // working (session fixation). The HTTP layer must re-issue the cookie with the
    // returned id (httpauth::attempt_login does both). If the store doesn't support
    // rename, the id is unchanged.
    std::string regenerate_id() {
        std::string fresh = new_session_id();
        if (store_->rename(id_, fresh)) id_ = fresh;
        return id_;
    }

private:
    SessionStore* store_;
    std::string id_;
};
