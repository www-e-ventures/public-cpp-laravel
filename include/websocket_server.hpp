// websocket_server.hpp — the live server-side WebSocket layer over a socket fd.
//
// `websocket.hpp` is the PURE protocol core (handshake key + frame encode/decode);
// this header is the thin socket layer that drives a real connection: read the
// Upgrade request, send `101 Switching Protocols`, then run a per-connection
// frame loop. It is kept SEPARATE from the framework core on purpose:
//
//   - The core `framework` library (HttpServer) must stay dependency-free. It
//     never includes this header, so it never links OpenSSL. Instead it hands a
//     WebSocket-Upgrade socket to an app-registered handler (see
//     HttpServer::on_websocket), and the *app* includes this header to drive it.
//   - Including this header pulls in OpenSSL (for the SHA-1 accept key, via
//     websocket.hpp) and POSIX sockets — same footprint the app already accepts
//     when it links the crypto-backed pieces.
//
// Threading: a Connection blocks its calling (worker) thread for the connection's
// lifetime. send_text() is serialised by an internal mutex, so a background
// thread (e.g. a Hub broadcast) may push to a connection while its owning thread
// is parked in receive(). recv()/send() on the same fd from two threads is safe;
// the mutex only protects against interleaved *writes*.
#pragma once
#include <sys/socket.h>
#include <unistd.h>

#include <cctype>
#include <functional>
#include <mutex>
#include <optional>
#include <set>
#include <string>

#include "http.hpp"
#include "websocket.hpp"

namespace ws {

// Case-insensitive header lookup (HTTP header names are case-insensitive, and the
// request parser preserves whatever casing the client sent).
inline std::string header_ci(const Request& req, const std::string& name) {
    for (const auto& kv : req.headers) {
        if (kv.first.size() != name.size()) continue;
        bool eq = true;
        for (std::size_t i = 0; i < name.size(); ++i) {
            if (std::tolower(static_cast<unsigned char>(kv.first[i])) !=
                std::tolower(static_cast<unsigned char>(name[i]))) {
                eq = false;
                break;
            }
        }
        if (eq) return kv.second;
    }
    return "";
}

// A live server-side WebSocket connection over an accepted socket `fd`. Build it
// from the fd and the parsed Upgrade request, call handshake() to send the 101,
// then exchange messages with send_text()/receive().
class Connection {
public:
    Connection(int fd, const Request& req)
        : fd_(fd), client_key_(header_ci(req, "Sec-WebSocket-Key")) {}

    Connection(const Connection&) = delete;
    Connection& operator=(const Connection&) = delete;

    // Complete the RFC 6455 handshake: reply 101 with the computed accept key.
    // Returns false if the request had no Sec-WebSocket-Key or the write failed.
    bool handshake() {
        if (client_key_.empty()) return false;
        const std::string resp =
            "HTTP/1.1 101 Switching Protocols\r\n"
            "Upgrade: websocket\r\n"
            "Connection: Upgrade\r\n"
            "Sec-WebSocket-Accept: " +
            accept_key(client_key_) + "\r\n\r\n";
        return send_all(resp);
    }

    // Send one text frame. Thread-safe with respect to other send_text() calls.
    bool send_text(const std::string& payload) {
        std::lock_guard<std::mutex> lock(send_mutex_);
        return send_all(encode_text(payload));
    }

    // Send one binary frame — for raw byte streams (e.g. a terminal's ANSI/CP437
    // output, which isn't valid UTF-8 and so can't ride in a text frame). Thread-safe
    // with respect to other sends, so a backend thread can push output while the
    // owning thread is parked in receive().
    bool send_binary(const std::string& bytes) {
        std::lock_guard<std::mutex> lock(send_mutex_);
        return send_all(encode_binary(bytes));
    }

    // Block for the next complete text/binary message, reassembling one that was
    // split across a leading data frame (FIN=0) plus Continuation frames, the last
    // with FIN=1 (RFC 6455 §5.4). Control frames are handled transparently and may
    // arrive between fragments: a Ping is answered with a Pong, a Close ends the
    // stream. A protocol violation — a Continuation with nothing in progress, or a
    // new data frame before the previous message finished — closes the connection.
    // Returns std::nullopt when the peer closes or the socket errors.
    std::optional<std::string> receive() {
        std::string message;       // accumulates a fragmented message's payload
        bool fragmenting = false;  // a data frame with FIN=0 opened a message
        for (;;) {
            Frame f = parse_frame(buf_);
            while (!f.ok) {
                char tmp[4096];
                ssize_t n = ::recv(fd_, tmp, sizeof(tmp), 0);
                if (n <= 0) return std::nullopt;
                buf_.append(tmp, static_cast<std::size_t>(n));
                f = parse_frame(buf_);
            }
            buf_.erase(0, f.consumed);
            switch (f.opcode) {
                case Opcode::Close:
                    send_control(encode_close());
                    return std::nullopt;
                case Opcode::Ping:
                    // Control frames may sit between data fragments; answering one
                    // must not disturb the message being reassembled.
                    send_control(encode_pong(f.payload));
                    continue;
                case Opcode::Pong:
                    continue;
                case Opcode::Text:
                case Opcode::Binary:
                    if (fragmenting) {  // a new message before the last one finished
                        send_control(encode_close());
                        return std::nullopt;
                    }
                    if (f.fin) return f.payload;  // whole message in a single frame
                    fragmenting = true;           // first fragment of a split message
                    message = f.payload;
                    continue;
                case Opcode::Continuation:
                    if (!fragmenting) {  // continuation with no message in progress
                        send_control(encode_close());
                        return std::nullopt;
                    }
                    message += f.payload;
                    if (f.fin) return message;  // final fragment — message complete
                    continue;
            }
        }
    }

    void close() {
        if (fd_ >= 0) {
            ::close(fd_);
            fd_ = -1;
        }
    }

    int fd() const { return fd_; }

private:
    bool send_all(const std::string& out) {
        std::size_t sent = 0;
        while (sent < out.size()) {
            ssize_t n = ::send(fd_, out.data() + sent, out.size() - sent, MSG_NOSIGNAL);
            if (n <= 0) return false;
            sent += static_cast<std::size_t>(n);
        }
        return true;
    }
    bool send_control(const std::string& out) {
        std::lock_guard<std::mutex> lock(send_mutex_);
        return send_all(out);
    }

    int fd_;
    std::string client_key_;
    std::string buf_;        // unconsumed bytes read from the socket
    std::mutex send_mutex_;  // serialises writes (owner thread + Hub broadcasts)
};

// A thread-safe registry of open connections — the fan-out point for
// "broadcast this to everyone" (pair with Broadcaster's channel events). Register
// a connection for its lifetime; broadcast() pushes one text frame to each.
class Hub {
public:
    void add(Connection* c) {
        std::lock_guard<std::mutex> lock(mutex_);
        conns_.insert(c);
    }
    void remove(Connection* c) {
        std::lock_guard<std::mutex> lock(mutex_);
        conns_.erase(c);
    }
    void broadcast(const std::string& text) {
        std::lock_guard<std::mutex> lock(mutex_);
        for (Connection* c : conns_) c->send_text(text);
    }
    void broadcast_binary(const std::string& bytes) {
        std::lock_guard<std::mutex> lock(mutex_);
        for (Connection* c : conns_) c->send_binary(bytes);
    }
    std::size_t size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return conns_.size();
    }

private:
    mutable std::mutex mutex_;
    std::set<Connection*> conns_;
};

// Bridge a connection as a byte "terminal": forward each inbound message (browser
// keystrokes / lines) to `on_input`, which is handed the connection so it can write
// output back with send_binary(). Blocks until the peer closes or the socket errors,
// then returns. This is the browser-terminal shape — a CoCo/BBS-style client dialing
// in over WebSocket and rendering the byte stream. For a backend that emits output
// unprompted (a live BBS/PTY, not just echo), push from another thread with
// conn.send_binary() while this loop reads input; sends are serialised.
inline void run_terminal(Connection& conn,
                         const std::function<void(Connection&, const std::string&)>& on_input) {
    while (auto msg = conn.receive()) on_input(conn, *msg);
}

} // namespace ws
