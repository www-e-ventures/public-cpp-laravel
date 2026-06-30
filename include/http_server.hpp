// http_server.hpp — a minimal blocking HTTP/1.1 server (zero dependencies)
//
// Bridges real sockets to the Kernel: read a request, Kernel::handle it, write the
// response. Parsing/formatting are pure functions (unit-tested in test_http_server)
// so the only untested part is the thin socket loop. Single-threaded, Connection:
// close — enough to reach the app from curl/a browser, not a production server.
#pragma once
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

class HttpServer {
public:
    HttpServer(std::shared_ptr<KernelContract> kernel, int port)
        : kernel_(std::move(kernel)), port_(port) {}

    // Blocks serving requests until the process is killed. Returns nonzero on a
    // socket setup failure (bind/listen). With workers > 1, runs a thread pool —
    // safe because the container and connections are thread-safe.
    int serve(int workers = 1);

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
    void handle_client(int client_fd);

    std::shared_ptr<KernelContract> kernel_;
    int port_;
    std::unordered_map<std::string, WebSocketHandler> ws_handlers_;
};
