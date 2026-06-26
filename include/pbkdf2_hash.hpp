// pbkdf2_hash.hpp — a real password hasher (PBKDF2-HMAC-SHA256) behind HashContract.
//
// Optional: needs OpenSSL (libcrypto). This is a proper salted key-derivation function
// with a work factor — production-appropriate (bcrypt/argon2 are alternatives). Drops
// in wherever a HashContract is expected; the dependency-free Hasher (hash.hpp) stays
// the default. Encoding: "pbkdf2$<iterations>$<salt-hex>$<derived-hex>".
#pragma once
#include <openssl/evp.h>
#include <openssl/rand.h>

#include <string>
#include <vector>

#include "hash.hpp"

class Pbkdf2Hasher : public HashContract {
public:
    explicit Pbkdf2Hasher(int iterations = 100000) : iterations_(iterations) {}

    std::string make(const std::string& plain) const override {
        unsigned char salt[16];
        RAND_bytes(salt, sizeof(salt));
        auto dk = derive(plain, salt, sizeof(salt), iterations_);
        return "pbkdf2$" + std::to_string(iterations_) + "$" + hex(salt, sizeof(salt)) + "$" +
               hex(dk.data(), dk.size());
    }

    bool check(const std::string& plain, const std::string& hashed) const override {
        auto parts = split(hashed, '$');
        if (parts.size() != 4 || parts[0] != "pbkdf2") return false;
        int iters = 0;
        try {
            iters = std::stoi(parts[1]);
        } catch (...) {
            return false;
        }
        auto salt = unhex(parts[2]);
        auto expected = unhex(parts[3]);
        auto dk = derive(plain, salt.data(), salt.size(), iters);
        return constant_time_equal(dk, expected);
    }

private:
    static std::vector<unsigned char> derive(const std::string& pw, const unsigned char* salt,
                                             std::size_t salt_len, int iters) {
        std::vector<unsigned char> out(32);
        PKCS5_PBKDF2_HMAC(pw.data(), static_cast<int>(pw.size()), salt,
                          static_cast<int>(salt_len), iters, EVP_sha256(),
                          static_cast<int>(out.size()), out.data());
        return out;
    }
    static std::string hex(const unsigned char* d, std::size_t n) {
        static const char* h = "0123456789abcdef";
        std::string s;
        s.reserve(n * 2);
        for (std::size_t i = 0; i < n; ++i) {
            s += h[d[i] >> 4];
            s += h[d[i] & 0xF];
        }
        return s;
    }
    static std::vector<unsigned char> unhex(const std::string& s) {
        std::vector<unsigned char> o;
        for (std::size_t i = 0; i + 1 < s.size(); i += 2)
            o.push_back(static_cast<unsigned char>(std::stoi(s.substr(i, 2), nullptr, 16)));
        return o;
    }
    static bool constant_time_equal(const std::vector<unsigned char>& a,
                                    const std::vector<unsigned char>& b) {
        if (a.size() != b.size()) return false;
        unsigned char r = 0;
        for (std::size_t i = 0; i < a.size(); ++i) r |= a[i] ^ b[i];
        return r == 0;
    }
    static std::vector<std::string> split(const std::string& s, char d) {
        std::vector<std::string> out;
        std::size_t i = 0;
        while (true) {
            auto p = s.find(d, i);
            out.push_back(s.substr(i, p == std::string::npos ? std::string::npos : p - i));
            if (p == std::string::npos) break;
            i = p + 1;
        }
        return out;
    }

    int iterations_;
};
