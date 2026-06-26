// test_websocket.cpp — RFC 6455 handshake + framing (separate exe; links OpenSSL).
#include "test_harness.hpp"

#include <string>

#include "websocket.hpp"

// The canonical RFC 6455 example.
TEST(websocket_accept_key_matches_rfc) {
    CHECK_EQ(ws::accept_key("dGhlIHNhbXBsZSBub25jZQ=="),
             std::string("s3pPLMBiTxaQ9kYGzzhZRbK+xOo="));
}

TEST(websocket_encode_text_frame) {
    std::string f = ws::encode_text("Hi");
    CHECK_EQ(static_cast<unsigned char>(f[0]), 0x81); // FIN + text
    CHECK_EQ(static_cast<unsigned char>(f[1]), 2);    // length, no mask bit
    CHECK_EQ(f.substr(2), std::string("Hi"));
}

TEST(websocket_decode_masked_client_frame) {
    unsigned char mask[4] = {0x11, 0x22, 0x33, 0x44};
    std::string payload = "ping";
    std::string f;
    f.push_back(static_cast<char>(0x81));
    f.push_back(static_cast<char>(0x80 | payload.size())); // masked
    for (unsigned char m : mask) f.push_back(static_cast<char>(m));
    for (std::size_t i = 0; i < payload.size(); ++i)
        f.push_back(static_cast<char>(payload[i] ^ mask[i % 4]));

    CHECK_EQ(ws::decode(f), std::string("ping"));
}

TEST(websocket_encode_then_decode_roundtrip) {
    // A server frame is unmasked; decode reads it straight back.
    CHECK_EQ(ws::decode(ws::encode_text("hello world")), std::string("hello world"));
}

int main() { return RUN_ALL_TESTS(); }
