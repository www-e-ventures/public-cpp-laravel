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

// Encode a server->client binary frame (FIN + binary opcode, unmasked). Use this for
// raw byte streams — a terminal's ANSI/CP437 output isn't valid UTF-8, so it can't
// ride in a text frame. Handles all three RFC 6455 length forms (so large output is
// fine, not just the < 64 KiB that encode_text covers).
inline std::string encode_binary(const std::string& payload) {
    std::string f;
    f.push_back(static_cast<char>(0x82)); // FIN=1, opcode=0x2 (binary)
    std::size_t n = payload.size();
    if (n < 126) {
        f.push_back(static_cast<char>(n));
    } else if (n <= 0xFFFF) {
        f.push_back(static_cast<char>(126));
        f.push_back(static_cast<char>((n >> 8) & 0xFF));
        f.push_back(static_cast<char>(n & 0xFF));
    } else {
        f.push_back(static_cast<char>(127));
        for (int i = 7; i >= 0; --i)
            f.push_back(static_cast<char>((static_cast<std::uint64_t>(n) >> (i * 8)) & 0xFF));
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

// --- Frame parsing for a live connection loop --------------------------------
// `decode()` above assumes a single complete text frame; a real server loop must
// also see the opcode (to answer Ping and notice Close) and cope with a buffer
// that holds a partial frame or several frames back-to-back. `parse_frame` reads
// ONE frame from the front of `buf`: on success it reports the opcode, the FIN
// bit (so a caller can reassemble a message split across frames), the payload,
// and how many bytes it consumed; if `buf` doesn't yet hold a whole frame it
// returns `ok == false` so the caller can read more bytes and try again.

enum class Opcode : unsigned char {
    Continuation = 0x0,
    Text = 0x1,
    Binary = 0x2,
    Close = 0x8,
    Ping = 0x9,
    Pong = 0xA,
};

struct Frame {
    Opcode opcode = Opcode::Text;
    std::string payload;
    bool fin = false;           // FIN bit: the last frame of a (maybe fragmented) message
    bool ok = false;            // a complete frame was parsed out of the buffer
    std::size_t consumed = 0;   // bytes the frame occupied at the front of `buf`
};

inline Frame parse_frame(const std::string& buf) {
    Frame f;
    if (buf.size() < 2) return f; // need at least the two-byte header
    auto byte = [&](std::size_t k) { return static_cast<unsigned char>(buf[k]); };

    f.fin = (byte(0) & 0x80) != 0;
    f.opcode = static_cast<Opcode>(byte(0) & 0x0F);
    bool masked = (byte(1) & 0x80) != 0;
    std::uint64_t len = byte(1) & 0x7F;
    std::size_t i = 2;
    if (len == 126) {
        if (buf.size() < 4) return f;
        len = (static_cast<std::uint64_t>(byte(2)) << 8) | byte(3);
        i = 4;
    } else if (len == 127) {
        if (buf.size() < 10) return f;
        len = 0;
        for (int k = 0; k < 8; ++k) len = (len << 8) | byte(2 + static_cast<std::size_t>(k));
        i = 10;
    }
    unsigned char mask[4] = {0, 0, 0, 0};
    if (masked) {
        if (buf.size() < i + 4) return f;
        for (int k = 0; k < 4; ++k) mask[k] = byte(i + static_cast<std::size_t>(k));
        i += 4;
    }
    if (buf.size() < i + len) return f; // payload not fully arrived yet

    f.payload.reserve(len);
    for (std::uint64_t j = 0; j < len; ++j) {
        unsigned char c = byte(i + j);
        f.payload.push_back(static_cast<char>(masked ? (c ^ mask[j % 4]) : c));
    }
    f.consumed = i + len;
    f.ok = true;
    return f;
}

// A server->client close frame (FIN + Close opcode, empty payload, unmasked).
inline std::string encode_close() {
    return std::string{static_cast<char>(0x88), static_cast<char>(0x00)};
}

// A server->client Pong echoing a Ping's payload (payloads are <= 125 by spec).
inline std::string encode_pong(const std::string& payload) {
    std::string f;
    f.push_back(static_cast<char>(0x8A)); // FIN + Pong opcode
    f.push_back(static_cast<char>(payload.size() & 0x7F));
    f += payload;
    return f;
}

} // namespace ws
