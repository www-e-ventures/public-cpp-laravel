// token.hpp — stateless signed tokens (HMAC-SHA256, JWT-shaped). Optional: needs
// OpenSSL (libcrypto), so it lives alongside pbkdf2_hash.hpp / websocket.hpp in the
// opt-in crypto layer — the framework core stays dependency-free.
//
// A service mints a compact token; ANY backend that shares the secret verifies it
// with no database round-trip. Claims are small: subject, abilities, issued-at,
// expiry. Abilities are plain strings with no built-in vocabulary — callers define
// their own ("room:library", "post", "api:read", ...), the same language used by
// personal_access_token.hpp and gate.hpp.
//
// Layout is a standard compact JWT: base64url(header).base64url(payload).base64url(sig),
// alg HS256 — so a stock JWT library on another backend can verify it too. HMAC is
// built from EVP_Digest (SHA-256) rather than the deprecated HMAC() to stay warning
// -clean on OpenSSL 3.
#pragma once
#include <openssl/evp.h>
#include <openssl/rand.h>

#include <cctype>
#include <cstdint>
#include <ctime>
#include <optional>
#include <string>
#include <vector>

#include "pipeline.hpp"

namespace tok {

struct Claims {
    std::string sub;                    // subject: a user id / handle
    std::vector<std::string> abilities; // e.g. {"post", "room:library"}
    std::int64_t iat = 0;               // issued-at (unix seconds)
    std::int64_t exp = 0;               // expiry (unix seconds); 0 = never expires

    bool can(const std::string& ability) const {
        for (const auto& a : abilities)
            if (a == ability || a == "*") return true;
        return false;
    }
};

// ── crypto + encoding primitives (also reused by personal_access_token.hpp) ──

inline std::string sha256_raw(const std::string& in) {
    unsigned char d[EVP_MAX_MD_SIZE];
    unsigned int n = 0;
    EVP_Digest(in.data(), in.size(), d, &n, EVP_sha256(), nullptr);
    return std::string(reinterpret_cast<char*>(d), n);
}

inline std::string sha256_hex(const std::string& in) {
    static const char* h = "0123456789abcdef";
    std::string raw = sha256_raw(in), s;
    s.reserve(raw.size() * 2);
    for (unsigned char c : raw) {
        s += h[c >> 4];
        s += h[c & 0xF];
    }
    return s;
}

// HMAC-SHA256, computed by hand (K' ⊕ opad ‖ H(K' ⊕ ipad ‖ msg)) over EVP_Digest.
inline std::string hmac_sha256(const std::string& key, const std::string& msg) {
    const std::size_t B = 64; // SHA-256 block size
    std::string k = key.size() > B ? sha256_raw(key) : key;
    k.resize(B, '\0');
    std::string ipad(B, '\0'), opad(B, '\0');
    for (std::size_t i = 0; i < B; ++i) {
        ipad[i] = static_cast<char>(static_cast<unsigned char>(k[i]) ^ 0x36);
        opad[i] = static_cast<char>(static_cast<unsigned char>(k[i]) ^ 0x5c);
    }
    return sha256_raw(opad + sha256_raw(ipad + msg));
}

namespace detail {

inline std::string b64url_encode(const std::string& in) {
    static const char* t =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    std::string out;
    int val = 0, bits = -6;
    for (unsigned char c : in) {
        val = (val << 8) + c;
        bits += 8;
        while (bits >= 0) {
            out.push_back(t[(val >> bits) & 0x3F]);
            bits -= 6;
        }
    }
    if (bits > -6) out.push_back(t[((val << 8) >> (bits + 8)) & 0x3F]);
    return out; // no '=' padding, per base64url convention
}

inline int b64url_val(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '-') return 62;
    if (c == '_') return 63;
    return -1;
}

inline std::string b64url_decode(const std::string& in) {
    std::string out;
    int val = 0, bits = -8;
    for (char c : in) {
        int d = b64url_val(c);
        if (d < 0) continue; // skip padding / stray chars leniently
        val = (val << 6) + d;
        bits += 6;
        if (bits >= 0) {
            out.push_back(static_cast<char>((val >> bits) & 0xFF));
            bits -= 8;
        }
    }
    return out;
}

inline bool constant_time_equal(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    unsigned char r = 0;
    for (std::size_t i = 0; i < a.size(); ++i)
        r |= static_cast<unsigned char>(a[i]) ^ static_cast<unsigned char>(b[i]);
    return r == 0;
}

inline std::string json_escape(const std::string& s) {
    std::string o;
    for (char c : s) {
        if (c == '"' || c == '\\') o += '\\';
        o += c;
    }
    return o;
}

inline std::string claims_json(const Claims& c) {
    std::string s = "{\"sub\":\"" + json_escape(c.sub) + "\",\"abilities\":[";
    for (std::size_t i = 0; i < c.abilities.size(); ++i) {
        if (i) s += ",";
        s += "\"" + json_escape(c.abilities[i]) + "\"";
    }
    s += "],\"iat\":" + std::to_string(c.iat) + ",\"exp\":" + std::to_string(c.exp) + "}";
    return s;
}

// Targeted extractors — run only AFTER the signature is verified, so leniency here
// is not a security concern (the payload is already authenticated).
inline std::string extract_string(const std::string& j, const std::string& key) {
    auto k = j.find("\"" + key + "\"");
    if (k == std::string::npos) return "";
    auto colon = j.find(':', k);
    if (colon == std::string::npos) return "";
    auto q1 = j.find('"', colon + 1);
    if (q1 == std::string::npos) return "";
    std::string out;
    for (std::size_t i = q1 + 1; i < j.size(); ++i) {
        if (j[i] == '\\' && i + 1 < j.size()) { out += j[i + 1]; ++i; }
        else if (j[i] == '"') break;
        else out += j[i];
    }
    return out;
}

inline std::int64_t extract_int(const std::string& j, const std::string& key) {
    auto k = j.find("\"" + key + "\"");
    if (k == std::string::npos) return 0;
    auto colon = j.find(':', k);
    if (colon == std::string::npos) return 0;
    std::size_t i = colon + 1;
    while (i < j.size() && j[i] == ' ') ++i;
    std::string num;
    if (i < j.size() && (j[i] == '-' || j[i] == '+')) num += j[i++];
    while (i < j.size() && j[i] >= '0' && j[i] <= '9') num += j[i++];
    try {
        return num.empty() ? 0 : std::stoll(num);
    } catch (...) {
        return 0;
    }
}

inline std::vector<std::string> extract_string_array(const std::string& j, const std::string& key) {
    std::vector<std::string> out;
    auto k = j.find("\"" + key + "\"");
    if (k == std::string::npos) return out;
    auto lb = j.find('[', k);
    if (lb == std::string::npos) return out;
    auto rb = j.find(']', lb);
    if (rb == std::string::npos) return out;
    std::size_t i = lb + 1;
    while (i < rb) {
        auto q1 = j.find('"', i);
        if (q1 == std::string::npos || q1 >= rb) break;
        std::string s;
        std::size_t p = q1 + 1;
        for (; p < j.size(); ++p) {
            if (j[p] == '\\' && p + 1 < j.size()) { s += j[p + 1]; ++p; }
            else if (j[p] == '"') break;
            else s += j[p];
        }
        out.push_back(s);
        i = p + 1;
    }
    return out;
}

inline Claims parse_claims(const std::string& json) {
    Claims c;
    c.sub = extract_string(json, "sub");
    c.iat = extract_int(json, "iat");
    c.exp = extract_int(json, "exp");
    c.abilities = extract_string_array(json, "abilities");
    return c;
}

} // namespace detail

// Sign a set of claims into a compact HS256 token.
inline std::string sign(const Claims& c, const std::string& secret) {
    static const std::string header = "{\"alg\":\"HS256\",\"typ\":\"JWT\"}";
    std::string signing_input =
        detail::b64url_encode(header) + "." + detail::b64url_encode(detail::claims_json(c));
    std::string sig = detail::b64url_encode(hmac_sha256(secret, signing_input));
    return signing_input + "." + sig;
}

// Verify a token's signature and expiry against `secret`. Returns the claims, or
// nullopt if the signature is wrong, the token is malformed, or it has expired.
// `now` (unix seconds) is injectable for tests; 0 means "use the wall clock".
inline std::optional<Claims> verify(const std::string& token, const std::string& secret,
                                    std::int64_t now = 0) {
    auto d1 = token.find('.');
    if (d1 == std::string::npos) return std::nullopt;
    auto d2 = token.find('.', d1 + 1);
    if (d2 == std::string::npos) return std::nullopt;
    std::string signing_input = token.substr(0, d2);
    std::string presented_sig = token.substr(d2 + 1);
    std::string expected_sig = detail::b64url_encode(hmac_sha256(secret, signing_input));
    if (!detail::constant_time_equal(presented_sig, expected_sig)) return std::nullopt;

    Claims c = detail::parse_claims(detail::b64url_decode(token.substr(d1 + 1, d2 - d1 - 1)));
    if (now == 0) now = static_cast<std::int64_t>(std::time(nullptr));
    if (c.exp != 0 && now > c.exp) return std::nullopt; // expired
    return c;
}

// A random URL-safe token string (default 32 bytes of entropy → ~43 chars). Used by
// personal_access_token.hpp for the plaintext token handed to the client once.
inline std::string random_token(std::size_t bytes = 32) {
    std::string raw(bytes, '\0');
    RAND_bytes(reinterpret_cast<unsigned char*>(&raw[0]), static_cast<int>(bytes));
    return detail::b64url_encode(raw);
}

// Pull the bearer token out of the Authorization header (case-insensitive; the
// "Bearer " prefix is optional so a raw token is tolerated).
inline std::string bearer_token(const Request& req) {
    for (const auto& kv : req.headers) {
        if (kv.first.size() != 13) continue;
        static const char* n = "authorization";
        bool eq = true;
        for (int i = 0; i < 13; ++i)
            if (std::tolower(static_cast<unsigned char>(kv.first[i])) != n[i]) { eq = false; break; }
        if (!eq) continue;
        const std::string& v = kv.second;
        const std::string prefix = "Bearer ";
        if (v.size() > prefix.size() && v.compare(0, prefix.size(), prefix) == 0)
            return v.substr(prefix.size());
        return v;
    }
    return "";
}

// Route middleware: require a valid signed token (Authorization: Bearer <jwt>) that
// carries `ability`. Rejects with 403 otherwise. Pair it with a route:
//   router->post("/boards/{id}/messages", handler, { tok::requires_ability("post", secret) });
inline Middleware requires_ability(const std::string& ability, const std::string& secret) {
    return [ability, secret](Request& req, Next next) -> Response {
        auto claims = verify(bearer_token(req), secret);
        if (!claims || !claims->can(ability)) return Response{403, "Forbidden"};
        return next(req);
    };
}

} // namespace tok
