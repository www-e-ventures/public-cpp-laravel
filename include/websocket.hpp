// websocket.hpp — WebSocket protocol primitives (RFC 6455) for a broadcast driver.
//
// The protocol core — the handshake accept key and frame encode/decode — is pure and
// unit-tested here. Wiring it to the dev HTTP server (detect the Upgrade request, send
// 101, then push frames per connection) is the thin socket layer, analogous to
// SmtpMailer::send. Needs OpenSSL (SHA-1 for the accept key).
#pragma once
#include <openssl/evp.h>

#include <cstdint>
#include <string>

namespace ws {

inline std::string base64(const unsigned char* data, std::size_t n) {
    static const char* t =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    int val = 0, bits = -6;
    for (std::size_t i = 0; i < n; ++i) {
        val = (val << 8) + data[i];
        bits += 8;
        while (bits >= 0) {
            out.push_back(t[(val >> bits) & 0x3F]);
            bits -= 6;
        }
    }
    if (bits > -6) out.push_back(t[((val << 8) >> (bits + 8)) & 0x3F]);
    while (out.size() % 4) out.push_back('=');
    return out;
}

// Sec-WebSocket-Accept = base64(SHA1(client-key + magic GUID)).
inline std::string accept_key(const std::string& client_key) {
    static const char* magic = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    std::string s = client_key + magic;
    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int len = 0;
    EVP_Digest(s.data(), s.size(), digest, &len, EVP_sha1(), nullptr);
    return base64(digest, len);
}

// Encode a server->client text frame (FIN + text opcode, unmasked).
inline std::string encode_text(const std::string& payload) {
    std::string f;
    f.push_back(static_cast<char>(0x81)); // FIN=1, opcode=0x1 (text)
    if (payload.size() < 126) {
        f.push_back(static_cast<char>(payload.size()));
    } else {
        f.push_back(static_cast<char>(126));
        f.push_back(static_cast<char>((payload.size() >> 8) & 0xFF));
        f.push_back(static_cast<char>(payload.size() & 0xFF));
    }
    f += payload;
    return f;
}

// Decode a frame's payload (handles client masking). Assumes a single complete frame.
inline std::string decode(const std::string& frame) {
    if (frame.size() < 2) return "";
    bool masked = (static_cast<unsigned char>(frame[1]) & 0x80) != 0;
    std::uint64_t len = static_cast<unsigned char>(frame[1]) & 0x7F;
    std::size_t i = 2;
    if (len == 126) {
        len = (static_cast<unsigned char>(frame[2]) << 8) | static_cast<unsigned char>(frame[3]);
        i = 4;
    } else if (len == 127) {
        len = 0;
        for (int k = 0; k < 8; ++k) len = (len << 8) | static_cast<unsigned char>(frame[2 + k]);
        i = 10;
    }
    unsigned char mask[4] = {0, 0, 0, 0};
    if (masked) {
        for (int k = 0; k < 4; ++k) mask[k] = static_cast<unsigned char>(frame[i + k]);
        i += 4;
    }
    std::string out;
    out.reserve(len);
    for (std::uint64_t j = 0; j < len && i + j < frame.size(); ++j) {
        unsigned char c = static_cast<unsigned char>(frame[i + j]);
        out.push_back(static_cast<char>(masked ? (c ^ mask[j % 4]) : c));
    }
    return out;
}

} // namespace ws
