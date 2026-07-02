// test_websocket.cpp — RFC 6455 handshake + framing (separate exe; links OpenSSL).
#include "test_harness.hpp"

#include <sys/socket.h>
#include <unistd.h>

#include <optional>
#include <string>
#include <thread>

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

TEST(websocket_encode_binary_frame_is_byte_exact) {
    std::string payload = std::string(1, '\0') + "\x80\xff" + "bytes"; // non-UTF-8
    std::string f = ws::encode_binary(payload);
    CHECK_EQ(static_cast<unsigned char>(f[0]), 0x82);                       // FIN + binary
    CHECK_EQ(static_cast<unsigned char>(f[1]), static_cast<unsigned char>(payload.size()));
    ws::Frame fr = ws::parse_frame(f); // server frames are unmasked
    CHECK(fr.ok);
    CHECK(fr.opcode == ws::Opcode::Binary);
    CHECK_EQ(fr.payload, payload); // byte-exact, incl. NUL + high bytes
}

TEST(websocket_connection_send_binary) {
    int sv[2];
    CHECK(::socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0);
    int server_fd = sv[0], client_fd = sv[1];
    Request req;
    req.headers["Sec-WebSocket-Key"] = "dGhlIHNhbXBsZSBub25jZQ==";
    ws::Connection conn(server_fd, req);
    CHECK(conn.handshake());
    char buf[512];
    ::recv(client_fd, buf, sizeof(buf), 0); // drain the 101

    // A CP437/ANSI-ish byte stream (not valid UTF-8) survives byte-exact.
    std::string bytes;
    bytes.push_back('\0');
    bytes.push_back(static_cast<char>(0x1b)); // ESC
    bytes += "[31mred";
    bytes.push_back(static_cast<char>(0xff));
    CHECK(conn.send_binary(bytes));

    ssize_t n = ::recv(client_fd, buf, sizeof(buf), 0);
    CHECK(n > 0);
    ws::Frame fr = ws::parse_frame(std::string(buf, static_cast<std::size_t>(n)));
    CHECK(fr.opcode == ws::Opcode::Binary);
    CHECK_EQ(fr.payload, bytes);

    conn.close();
    ::close(client_fd);
}

TEST(websocket_run_terminal_echoes_input_as_binary) {
    int sv[2];
    CHECK(::socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0);
    int server_fd = sv[0], client_fd = sv[1];
    Request req;
    req.headers["Sec-WebSocket-Key"] = "dGhlIHNhbXBsZSBub25jZQ==";
    ws::Connection conn(server_fd, req);
    CHECK(conn.handshake());
    char buf[512];
    ::recv(client_fd, buf, sizeof(buf), 0); // drain the 101

    std::thread t([&] {
        ws::run_terminal(conn, [](ws::Connection& c, const std::string& in) {
            c.send_binary("[" + in + "]"); // echo each line, wrapped
        });
    });

    std::string in = masked_frame(0x1, "hi");
    ::send(client_fd, in.data(), in.size(), 0);
    ssize_t n = ::recv(client_fd, buf, sizeof(buf), 0);
    CHECK(n > 0);
    ws::Frame fr = ws::parse_frame(std::string(buf, static_cast<std::size_t>(n)));
    CHECK(fr.opcode == ws::Opcode::Binary);
    CHECK_EQ(fr.payload, std::string("[hi]"));

    // Client closes -> receive() returns nullopt -> run_terminal returns -> thread joins.
    std::string close = masked_frame(0x8, "");
    ::send(client_fd, close.data(), close.size(), 0);
    t.join();

    conn.close();
    ::close(client_fd);
}

// --- Size caps, masking enforcement, close-code echo (v0.7.0 hardening) ------

TEST(websocket_parse_frame_flags_oversized_declared_length) {
    // Only the HEADER of a huge frame arrives; the declared length alone must
    // trip the cap — the server refuses before buffering any payload.
    std::string header;
    header.push_back(static_cast<char>(0x82));        // FIN + binary
    header.push_back(static_cast<char>(0x80 | 127));  // masked + 64-bit length form
    for (int i = 7; i >= 0; --i)                      // declared length: 1 GiB
        header.push_back(static_cast<char>((static_cast<std::uint64_t>(1 << 30) >> (i * 8)) & 0xFF));

    ws::Frame f = ws::parse_frame(header, /*max_payload=*/1024 * 1024);
    CHECK(!f.ok);
    CHECK(f.too_big);

    // Without a cap the same header just reads as incomplete, as before.
    ws::Frame g = ws::parse_frame(header);
    CHECK(!g.ok);
    CHECK(!g.too_big);
}

TEST(websocket_parse_frame_reports_mask_bit) {
    ws::Frame client = ws::parse_frame(masked_frame(0x1, "hi"));
    CHECK(client.ok);
    CHECK(client.masked);
    ws::Frame server = ws::parse_frame(ws::encode_text("hi")); // server frames unmasked
    CHECK(server.ok);
    CHECK(!server.masked);
}

TEST(websocket_encode_close_with_status_code) {
    std::string c = ws::encode_close(1009);
    CHECK_EQ(static_cast<unsigned char>(c[0]), 0x88); // FIN + Close
    CHECK_EQ(static_cast<unsigned char>(c[1]), 0x02); // 2-byte payload
    CHECK_EQ(static_cast<unsigned char>(c[2]), 0x03); // 1009 >> 8
    CHECK_EQ(static_cast<unsigned char>(c[3]), 0xF1); // 1009 & 0xFF
}

TEST(websocket_connection_closes_1009_on_oversized_message) {
    int sv[2];
    CHECK(::socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0);
    Request req;
    req.headers["Sec-WebSocket-Key"] = "dGhlIHNhbXBsZSBub25jZQ==";
    ws::Connection conn(sv[0], req);
    CHECK(conn.handshake());
    char buf[512];
    ::recv(sv[1], buf, sizeof(buf), 0); // drain the 101

    conn.set_max_message_bytes(8);
    std::string big = masked_frame(0x2, "0123456789"); // 10 > 8
    ::send(sv[1], big.data(), big.size(), 0);
    CHECK(!conn.receive().has_value());

    ssize_t n = ::recv(sv[1], buf, sizeof(buf), 0);
    CHECK(n >= 4);
    CHECK_EQ(static_cast<unsigned char>(buf[0]), 0x88); // Close
    CHECK_EQ((static_cast<unsigned char>(buf[2]) << 8) | static_cast<unsigned char>(buf[3]),
             1009); // Message Too Big

    conn.close();
    ::close(sv[1]);
}

TEST(websocket_connection_closes_1009_when_fragments_sum_past_cap) {
    int sv[2];
    CHECK(::socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0);
    Request req;
    req.headers["Sec-WebSocket-Key"] = "dGhlIHNhbXBsZSBub25jZQ==";
    ws::Connection conn(sv[0], req);
    CHECK(conn.handshake());
    char buf[512];
    ::recv(sv[1], buf, sizeof(buf), 0); // drain the 101

    // Each fragment fits the 8-byte cap on its own; together they exceed it.
    conn.set_max_message_bytes(8);
    std::string a = masked_frame(0x1, "01234", false);
    std::string b = masked_frame(0x0, "56789", true);
    ::send(sv[1], a.data(), a.size(), 0);
    ::send(sv[1], b.data(), b.size(), 0);
    CHECK(!conn.receive().has_value());

    ssize_t n = ::recv(sv[1], buf, sizeof(buf), 0);
    CHECK(n >= 4);
    CHECK_EQ(static_cast<unsigned char>(buf[0]), 0x88);
    CHECK_EQ((static_cast<unsigned char>(buf[2]) << 8) | static_cast<unsigned char>(buf[3]),
             1009);

    conn.close();
    ::close(sv[1]);
}

TEST(websocket_connection_rejects_unmasked_client_frame) {
    int sv[2];
    CHECK(::socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0);
    Request req;
    req.headers["Sec-WebSocket-Key"] = "dGhlIHNhbXBsZSBub25jZQ==";
    ws::Connection conn(sv[0], req);
    CHECK(conn.handshake());
    char buf[512];
    ::recv(sv[1], buf, sizeof(buf), 0); // drain the 101

    // A client MUST mask (RFC 6455 §5.1); an unmasked data frame is a protocol
    // error and the server closes with 1002.
    std::string unmasked = ws::encode_text("cheeky");
    ::send(sv[1], unmasked.data(), unmasked.size(), 0);
    CHECK(!conn.receive().has_value());

    ssize_t n = ::recv(sv[1], buf, sizeof(buf), 0);
    CHECK(n >= 4);
    CHECK_EQ(static_cast<unsigned char>(buf[0]), 0x88);
    CHECK_EQ((static_cast<unsigned char>(buf[2]) << 8) | static_cast<unsigned char>(buf[3]),
             1002); // Protocol Error

    conn.close();
    ::close(sv[1]);
}

TEST(websocket_connection_accepts_unmasked_when_opted_out) {
    int sv[2];
    CHECK(::socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0);
    Request req;
    req.headers["Sec-WebSocket-Key"] = "dGhlIHNhbXBsZSBub25jZQ==";
    ws::Connection conn(sv[0], req);
    CHECK(conn.handshake());
    char buf[512];
    ::recv(sv[1], buf, sizeof(buf), 0); // drain the 101

    conn.set_require_masked(false); // server-to-server / test-harness peers
    std::string unmasked = ws::encode_text("fine");
    ::send(sv[1], unmasked.data(), unmasked.size(), 0);
    auto msg = conn.receive();
    CHECK(msg.has_value());
    CHECK_EQ(*msg, std::string("fine"));

    conn.close();
    ::close(sv[1]);
}

TEST(websocket_connection_echoes_peer_close_code) {
    int sv[2];
    CHECK(::socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0);
    Request req;
    req.headers["Sec-WebSocket-Key"] = "dGhlIHNhbXBsZSBub25jZQ==";
    ws::Connection conn(sv[0], req);
    CHECK(conn.handshake());
    char buf[512];
    ::recv(sv[1], buf, sizeof(buf), 0); // drain the 101

    // Peer closes with 1000 (normal); the reply must carry 1000 back, not be empty.
    std::string payload{static_cast<char>(0x03), static_cast<char>(0xE8)}; // 1000
    std::string close = masked_frame(0x8, payload);
    ::send(sv[1], close.data(), close.size(), 0);
    CHECK(!conn.receive().has_value());

    ssize_t n = ::recv(sv[1], buf, sizeof(buf), 0);
    CHECK(n >= 4);
    CHECK_EQ(static_cast<unsigned char>(buf[0]), 0x88);
    CHECK_EQ(static_cast<unsigned char>(buf[1]), 0x02);
    CHECK_EQ((static_cast<unsigned char>(buf[2]) << 8) | static_cast<unsigned char>(buf[3]),
             1000);

    conn.close();
    ::close(sv[1]);
}


TEST(websocket_origin_allowlist_gates_handshake) {
    // Allowed origin: the upgrade proceeds normally.
    {
        int sv[2];
        CHECK(::socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0);
        Request req;
        req.headers["Sec-WebSocket-Key"] = "dGhlIHNhbXBsZSBub25jZQ==";
        req.headers["Origin"] = "https://bbs.example";
        ws::Connection conn(sv[0], req);
        conn.set_allowed_origins({"https://bbs.example", "https://coco.example"});
        CHECK(conn.handshake());
        char buf[512];
        ssize_t n = ::recv(sv[1], buf, sizeof(buf), 0);
        CHECK(n > 0);
        CHECK(std::string(buf, static_cast<std::size_t>(n)).find("101 Switching Protocols") !=
              std::string::npos);
        conn.close();
        ::close(sv[1]);
    }
    // Foreign origin (a hostile page scripting a WS with the victim's cookies):
    // refused with a 403, no upgrade.
    {
        int sv[2];
        CHECK(::socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0);
        Request req;
        req.headers["Sec-WebSocket-Key"] = "dGhlIHNhbXBsZSBub25jZQ==";
        req.headers["Origin"] = "https://evil.example";
        ws::Connection conn(sv[0], req);
        conn.set_allowed_origins({"https://bbs.example"});
        CHECK(!conn.handshake());
        char buf[512];
        ssize_t n = ::recv(sv[1], buf, sizeof(buf), 0);
        CHECK(n > 0);
        CHECK(std::string(buf, static_cast<std::size_t>(n)).find("403 Forbidden") !=
              std::string::npos);
        conn.close();
        ::close(sv[1]);
    }
    // No allowlist (the default): any or no Origin is accepted, as before.
    {
        int sv[2];
        CHECK(::socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0);
        Request req;
        req.headers["Sec-WebSocket-Key"] = "dGhlIHNhbXBsZSBub25jZQ==";
        ws::Connection conn(sv[0], req);
        CHECK(conn.handshake());
        conn.close();
        ::close(sv[1]);
    }
}

int main() { return RUN_ALL_TESTS(); }
