// test_websocket.cpp — RFC 6455 handshake + framing (separate exe; links OpenSSL).
#include "test_harness.hpp"

#include <sys/socket.h>
#include <unistd.h>

#include <optional>
#include <string>

#include "http.hpp"
#include "websocket.hpp"
#include "websocket_server.hpp"

// Build a masked client->server frame (clients MUST mask, per RFC 6455). `fin`
// defaults to a final frame; clear it to build a non-final fragment. Payloads in
// these tests are < 126 bytes, so a single length byte suffices.
static std::string masked_frame(unsigned char opcode, const std::string& payload, bool fin = true) {
    const unsigned char mask[4] = {0xAB, 0xCD, 0xEF, 0x12};
    std::string f;
    f.push_back(static_cast<char>((fin ? 0x80 : 0x00) | opcode));  // FIN? + opcode
    f.push_back(static_cast<char>(0x80 | payload.size()));         // mask bit + len
    for (unsigned char m : mask) f.push_back(static_cast<char>(m));
    for (std::size_t i = 0; i < payload.size(); ++i)
        f.push_back(static_cast<char>(payload[i] ^ mask[i % 4]));
    return f;
}

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

// --- Opcode-aware frame parsing (the live loop's primitive) ------------------

TEST(websocket_parse_frame_reads_opcode_payload_and_length) {
    std::string f = masked_frame(0x1, "ping");
    ws::Frame fr = ws::parse_frame(f);
    CHECK(fr.ok);
    CHECK(fr.opcode == ws::Opcode::Text);
    CHECK_EQ(fr.payload, std::string("ping"));
    CHECK_EQ(fr.consumed, f.size());
}

TEST(websocket_parse_frame_reports_incomplete) {
    std::string f = masked_frame(0x1, "abcdef");
    ws::Frame partial = ws::parse_frame(f.substr(0, 4)); // truncated mid-frame
    CHECK(!partial.ok);
    CHECK_EQ(partial.consumed, static_cast<std::size_t>(0));
}

TEST(websocket_parse_frame_reports_fin_bit) {
    ws::Frame final_frame = ws::parse_frame(masked_frame(0x1, "x"));        // FIN=1
    CHECK(final_frame.ok);
    CHECK(final_frame.fin);
    ws::Frame fragment = ws::parse_frame(masked_frame(0x1, "x", false));    // FIN=0
    CHECK(fragment.ok);
    CHECK(!fragment.fin);
}

TEST(websocket_encode_close_and_pong) {
    std::string c = ws::encode_close();
    CHECK_EQ(static_cast<unsigned char>(c[0]), 0x88); // FIN + Close
    CHECK_EQ(static_cast<unsigned char>(c[1]), 0x00); // empty payload
    std::string p = ws::encode_pong("hi");
    CHECK_EQ(static_cast<unsigned char>(p[0]), 0x8A); // FIN + Pong
    CHECK_EQ(static_cast<unsigned char>(p[1]), 2);
    CHECK_EQ(p.substr(2), std::string("hi"));
}

// --- The live connection, over a real socketpair (no network) ----------------

TEST(websocket_connection_handshake_and_message_exchange) {
    int sv[2];
    CHECK(::socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0);
    int server_fd = sv[0], client_fd = sv[1];

    Request req;
    req.headers["Sec-WebSocket-Key"] = "dGhlIHNhbXBsZSBub25jZQ==";
    ws::Connection conn(server_fd, req);
    CHECK(conn.handshake());

    // The client sees a well-formed 101 with the RFC accept key.
    char buf[2048];
    ssize_t n = ::recv(client_fd, buf, sizeof(buf), 0);
    CHECK(n > 0);
    std::string resp(buf, static_cast<std::size_t>(n));
    CHECK(resp.find("101 Switching Protocols") != std::string::npos);
    CHECK(resp.find("Sec-WebSocket-Accept: s3pPLMBiTxaQ9kYGzzhZRbK+xOo=") != std::string::npos);

    // Client -> server: a masked text frame is received and unmasked.
    std::string in = masked_frame(0x1, "hello");
    ::send(client_fd, in.data(), in.size(), 0);
    std::optional<std::string> msg = conn.receive();
    CHECK(msg.has_value());
    CHECK_EQ(*msg, std::string("hello"));

    // Server -> client: send_text produces a frame the client can decode.
    CHECK(conn.send_text("world"));
    n = ::recv(client_fd, buf, sizeof(buf), 0);
    CHECK(n > 0);
    CHECK_EQ(ws::decode(std::string(buf, static_cast<std::size_t>(n))), std::string("world"));

    conn.close();
    ::close(client_fd);
}

TEST(websocket_connection_answers_ping_then_handles_close) {
    int sv[2];
    CHECK(::socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0);
    int server_fd = sv[0], client_fd = sv[1];

    Request req;
    req.headers["Sec-WebSocket-Key"] = "dGhlIHNhbXBsZSBub25jZQ==";
    ws::Connection conn(server_fd, req);
    CHECK(conn.handshake());
    char buf[2048];
    ::recv(client_fd, buf, sizeof(buf), 0); // drain the 101

    // A Ping is answered transparently; the next data frame is what receive() returns.
    std::string ping = masked_frame(0x9, "png");
    std::string text = masked_frame(0x1, "after-ping");
    ::send(client_fd, ping.data(), ping.size(), 0);
    ::send(client_fd, text.data(), text.size(), 0);
    std::optional<std::string> msg = conn.receive();
    CHECK(msg.has_value());
    CHECK_EQ(*msg, std::string("after-ping"));

    // The server should have written a Pong back in response to the Ping.
    ssize_t n = ::recv(client_fd, buf, sizeof(buf), 0);
    CHECK(n >= 2);
    CHECK_EQ(static_cast<unsigned char>(buf[0]), 0x8A); // Pong

    // A Close frame ends the stream: receive() returns nullopt.
    std::string close = masked_frame(0x8, "");
    ::send(client_fd, close.data(), close.size(), 0);
    CHECK(!conn.receive().has_value());

    conn.close();
    ::close(client_fd);
}

TEST(websocket_connection_reassembles_fragmented_message) {
    int sv[2];
    CHECK(::socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0);
    int server_fd = sv[0], client_fd = sv[1];

    Request req;
    req.headers["Sec-WebSocket-Key"] = "dGhlIHNhbXBsZSBub25jZQ==";
    ws::Connection conn(server_fd, req);
    CHECK(conn.handshake());
    char buf[2048];
    ::recv(client_fd, buf, sizeof(buf), 0); // drain the 101

    // A message split as Text(FIN=0) + Continuation(FIN=0) + Continuation(FIN=1)
    // is reassembled into one payload.
    std::string a = masked_frame(0x1, "Hel", false);
    std::string b = masked_frame(0x0, "lo ", false);
    std::string c = masked_frame(0x0, "world", true);
    ::send(client_fd, a.data(), a.size(), 0);
    ::send(client_fd, b.data(), b.size(), 0);
    ::send(client_fd, c.data(), c.size(), 0);
    std::optional<std::string> msg = conn.receive();
    CHECK(msg.has_value());
    CHECK_EQ(*msg, std::string("Hello world"));

    conn.close();
    ::close(client_fd);
}

TEST(websocket_connection_handles_ping_between_fragments) {
    int sv[2];
    CHECK(::socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0);
    int server_fd = sv[0], client_fd = sv[1];

    Request req;
    req.headers["Sec-WebSocket-Key"] = "dGhlIHNhbXBsZSBub25jZQ==";
    ws::Connection conn(server_fd, req);
    CHECK(conn.handshake());
    char buf[2048];
    ::recv(client_fd, buf, sizeof(buf), 0); // drain the 101

    // A Ping interleaved between two fragments is answered without breaking the
    // reassembly of the surrounding message.
    std::string a = masked_frame(0x1, "foo", false);
    std::string ping = masked_frame(0x9, "hb");
    std::string b = masked_frame(0x0, "bar", true);
    ::send(client_fd, a.data(), a.size(), 0);
    ::send(client_fd, ping.data(), ping.size(), 0);
    ::send(client_fd, b.data(), b.size(), 0);
    std::optional<std::string> msg = conn.receive();
    CHECK(msg.has_value());
    CHECK_EQ(*msg, std::string("foobar"));

    // The Pong for the interleaved Ping was written back.
    ssize_t n = ::recv(client_fd, buf, sizeof(buf), 0);
    CHECK(n >= 2);
    CHECK_EQ(static_cast<unsigned char>(buf[0]), 0x8A);            // Pong
    CHECK_EQ(static_cast<unsigned char>(buf[1]) & 0x7F, 2);        // payload "hb"

    conn.close();
    ::close(client_fd);
}

TEST(websocket_connection_rejects_stray_continuation) {
    int sv[2];
    CHECK(::socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0);
    int server_fd = sv[0], client_fd = sv[1];

    Request req;
    req.headers["Sec-WebSocket-Key"] = "dGhlIHNhbXBsZSBub25jZQ==";
    ws::Connection conn(server_fd, req);
    CHECK(conn.handshake());
    char buf[2048];
    ::recv(client_fd, buf, sizeof(buf), 0); // drain the 101

    // A Continuation with no message in progress is a protocol error: the server
    // closes the connection and receive() reports end-of-stream.
    std::string stray = masked_frame(0x0, "oops", true);
    ::send(client_fd, stray.data(), stray.size(), 0);
    CHECK(!conn.receive().has_value());
    ssize_t n = ::recv(client_fd, buf, sizeof(buf), 0);
    CHECK(n >= 2);
    CHECK_EQ(static_cast<unsigned char>(buf[0]), 0x88);           // Close

    conn.close();
    ::close(client_fd);
}

TEST(websocket_hub_broadcasts_to_all_connections) {
    int a[2], b[2];
    CHECK(::socketpair(AF_UNIX, SOCK_STREAM, 0, a) == 0);
    CHECK(::socketpair(AF_UNIX, SOCK_STREAM, 0, b) == 0);
    Request req;
    req.headers["Sec-WebSocket-Key"] = "dGhlIHNhbXBsZSBub25jZQ==";
    ws::Connection c1(a[0], req), c2(b[0], req);

    ws::Hub hub;
    hub.add(&c1);
    hub.add(&c2);
    CHECK_EQ(hub.size(), static_cast<std::size_t>(2));
    hub.broadcast("notice");

    for (int client : {a[1], b[1]}) {
        char buf[256];
        ssize_t n = ::recv(client, buf, sizeof(buf), 0);
        CHECK(n > 0);
        CHECK_EQ(ws::decode(std::string(buf, static_cast<std::size_t>(n))), std::string("notice"));
    }

    hub.remove(&c1);
    hub.remove(&c2);
    c1.close();
    c2.close();
    ::close(a[1]);
    ::close(b[1]);
}

int main() { return RUN_ALL_TESTS(); }
