// http_server.hpp — a minimal blocking HTTP/1.1 server (zero dependencies)
//
// Bridges real sockets to the Kernel: read a request, Kernel::handle it, write the
// response. Parsing/formatting are pure functions (unit-tested in test_http_server)
// so the only untested part is the thin socket loop. Blocking, Connection: close,
// optionally multi-worker — sized for an admin dashboard / JSON API, not a hostile
// internet edge (run one behind a reverse proxy). A worker survives a throwing
// handler (500), refuses oversized requests (413/431), and times out a stalled
// read, so one bad client can't take the process or a worker hostage.
#pragma once
#include <atomic>
#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>

#include "http.hpp"
#include "kernel_contract.hpp"

// Parse a raw HTTP request (request line + headers + body) into a Request.
std::optional<Request> parse_http_request(const std::string& raw);

// Serialise a Response as an HTTP/1.1 message (sets Content-Length, Connection: close).
std::string format_http_response(const Response& res);

// Case-insensitive Content-Length from a raw header block ("Content-length: 42" is
// as valid as the canonical casing). nullopt when the header is absent/unparseable.
std::optional<std::size_t> parse_content_length(const std::string& head);

// True if the header block declares Transfer-Encoding: chunked. The server doesn't
// speak chunked (bodies are read by Content-Length); it answers 411 instead of
// hanging waiting for a length that will never come.
bool has_chunked_body(const std::string& head);

class HttpServer {
public:
    // Per-request ceilings. Defaults suit the real consumers (form posts, JSON,
    // save-state blobs of a few MB) with headroom; raise/lower via set_limits()
    // BEFORE serve(). A zero read_timeout_seconds disables the timeout.
    struct Limits {
        std::size_t max_header_bytes = 64 * 1024;        // breach → 431, close
        std::size_t max_body_bytes = 16 * 1024 * 1024;   // breach → 413, close
        int read_timeout_seconds = 30;                   // stalled read → close
    };

    HttpServer(std::shared_ptr<KernelContract> kernel, int port)
        : kernel_(std::move(kernel)), port_(port) {}

    void set_limits(Limits limits) { limits_ = limits; }
    const Limits& limits() const { return limits_; }

    // Blocks serving requests until the process is killed. Returns nonzero on a
    // socket setup failure (bind/listen). With workers > 1, runs a thread pool —
    // safe because the container and connections are thread-safe.
    int serve(int workers = 1);

    // Stop a running serve(): close the listening socket and break the accept loop
    // so serve() returns. Meant to be called from another thread (e.g. a signal
    // handler or a dedicated shutdown thread) while serve() blocks. Idempotent.
    void stop();

    // A WebSocket endpoint. When a request to `path` carries the WebSocket
    // Upgrade headers, the server hands the raw client socket and the parsed
    // request to `handler` instead of running the normal request->response cycle.
    // The handler owns the socket for the connection's lifetime and must close
    // it when done (the ws::Connection helper in websocket_server.hpp does the
    // handshake + frame loop). The core deliberately keeps this as a raw-fd
    // callback so it depends on neither OpenSSL nor the WebSocket framing code —
    // those live with the handler. Register endpoints BEFORE serve(); the map is
    // read-only once worker threads are running.
    using WebSocketHandler = std::function<void(int client_fd, const Request& req)>;
    void on_websocket(const std::string& path, WebSocketHandler handler) {
        ws_handlers_[path] = std::move(handler);
    }

private:
    void accept_loop(int server_fd);
    void handle_client(int client_fd, const std::string& remote_addr);

    std::shared_ptr<KernelContract> kernel_;
    int port_;
    Limits limits_{};
    std::unordered_map<std::string, WebSocketHandler> ws_handlers_;
    std::atomic<bool> running_{false};
    std::atomic<int> server_fd_{-1};
};
