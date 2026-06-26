// hash.hpp — password hashing (Laravel: Illuminate\Contracts\Hashing\Hasher).
//
// HashContract::make produces a salted hash; check() verifies a plaintext against one.
//
// NOTE: the default Hasher uses salted FNV-1a, which is NOT cryptographically secure —
// it demonstrates the salt + verify *shape*. A production hasher would use bcrypt or
// argon2 (a sanctioned dependency); it can drop in behind this same contract.
#pragma once
#include <cstdint>
#include <random>
#include <string>

class HashContract {
public:
    virtual ~HashContract() = default;
    virtual std::string make(const std::string& plain) const = 0;
    virtual bool check(const std::string& plain, const std::string& hashed) const = 0;
};

class Hasher : public HashContract {
public:
    std::string make(const std::string& plain) const override {
        std::string salt = random_salt();
        return salt + "$" + digest(salt + plain);
    }
    bool check(const std::string& plain, const std::string& hashed) const override {
        auto pos = hashed.find('$');
        if (pos == std::string::npos) return false;
        std::string salt = hashed.substr(0, pos);
        return hashed == salt + "$" + digest(salt + plain);
    }

private:
    static std::string to_hex(std::uint64_t v) {
        static const char* d = "0123456789abcdef";
        std::string out(16, '0');
        for (int i = 15; i >= 0; --i) {
            out[i] = d[v & 0xF];
            v >>= 4;
        }
        return out;
    }
    static std::string digest(const std::string& s) {
        std::uint64_t h = 1469598103934665603ULL; // FNV-1a
        for (unsigned char c : s) {
            h ^= c;
            h *= 1099511628211ULL;
        }
        return to_hex(h);
    }
    static std::string random_salt() {
        static thread_local std::mt19937_64 rng(std::random_device{}());
        return to_hex(rng());
    }
};
