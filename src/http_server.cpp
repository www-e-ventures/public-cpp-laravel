// http_server.cpp — see http_server.hpp
#include "http_server.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>
#include <vector>

namespace {

std::string reason_phrase(int status) {
    switch (status) {
        case 200: return "OK";
        case 201: return "Created";
        case 206: return "Partial Content";
        case 304: return "Not Modified";
        case 400: return "Bad Request";
        case 401: return "Unauthorized";
        case 403: return "Forbidden";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        case 411: return "Length Required";
        case 413: return "Payload Too Large";
        case 416: return "Range Not Satisfiable";
        case 422: return "Unprocessable Entity";
        case 431: return "Request Header Fields Too Large";
        case 500: return "Internal Server Error";
        default:  return "OK";
    }
}

// trim leading/trailing spaces (header values).
std::string trim(const std::string& s) {
    auto b = s.find_first_not_of(" \t");
    if (b == std::string::npos) return "";
    auto e = s.find_last_not_of(" \t");
    return s.substr(b, e - b + 1);
}

// Lower-case copy — header names (and the tokens in their values) compare
// case-insensitively per RFC 9110.
std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// The value of header `name_lower` in a raw header block, or nullopt. Matches at
// line starts only ("\r\nname:"), so a value can't spoof a later header.
std::optional<std::string> header_value(const std::string& head, const std::string& name_lower) {
    std::string h = lower(head);
    std::string needle = "\r\n" + name_lower + ":";
    auto pos = h.find(needle);
    if (pos == std::string::npos) return std::nullopt;
    auto vstart = pos + needle.size();
    auto eol = head.find("\r\n", vstart);
    return trim(head.substr(vstart, eol == std::string::npos ? std::string::npos : eol - vstart));
}

} // namespace

std::optional<std::size_t> parse_content_length(const std::string& head) {
    auto v = header_value(head, "content-length");
    if (!v || v->empty()) return std::nullopt;
    for (char c : *v)
        if (c < '0' || c > '9') return std::nullopt;
    errno = 0;
    unsigned long long n = std::strtoull(v->c_str(), nullptr, 10);
    if (errno != 0) return std::nullopt;
    return static_cast<std::size_t>(n);
}

bool has_chunked_body(const std::string& head) {
    auto v = header_value(head, "transfer-encoding");
    return v && lower(*v).find("chunked") != std::string::npos;
}

std::optional<Request> parse_http_request(const std::string& raw) {
    auto header_end = raw.find("\r\n\r\n");
    std::string head = header_end == std::string::npos ? raw : raw.substr(0, header_end);
    std::string body = header_end == std::string::npos ? "" : raw.substr(header_end + 4);

    auto line_end = head.find("\r\n");
    std::string request_line = head.substr(0, line_end);

    // METHOD SP TARGET SP VERSION
    auto sp1 = request_line.find(' ');
    auto sp2 = request_line.find(' ', sp1 == std::string::npos ? sp1 : sp1 + 1);
    if (sp1 == std::string::npos || sp2 == std::string::npos) return std::nullopt;

    Request req;
    req.method = request_line.substr(0, sp1);
    std::string target = request_line.substr(sp1 + 1, sp2 - sp1 - 1);
    // Keep req.path query-free; parse the query string into req.query (url-decoded
    // like a form body). Callers read filters/pagination off req.query.
    auto qmark = target.find('?');
    req.path = target.substr(0, qmark);
    if (qmark != std::string::npos)
        req.query = parse_pairs(target.substr(qmark + 1), '&', /*decode=*/true);
    req.body = body;

    std::size_t pos = (line_end == std::string::npos) ? head.size() : line_end + 2;
    while (pos < head.size()) {
        auto nl = head.find("\r\n", pos);
        std::string line = head.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos);
        auto colon = line.find(':');
        if (colon != std::string::npos)
            req.headers[trim(line.substr(0, colon))] = trim(line.substr(colon + 1));
        if (nl == std::string::npos) break;
        pos = nl + 2;
    }
    return req;
}

std::string format_http_response(const Response& res) {
    std::string out = "HTTP/1.1 " + std::to_string(res.status) + " " + reason_phrase(res.status) +
                      "\r\n";
    for (const auto& h : res.headers) out += h.first + ": " + h.second + "\r\n";
    out += "Content-Length: " + std::to_string(res.body.size()) + "\r\n";
    out += "Connection: close\r\n\r\n";
    out += res.body;
    return out;
}

namespace {

// Outcome of reading one request off the wire. Anything but Ok short-circuits the
// request cycle: the caller answers with the paired status (or, for Closed, just
// hangs up — there's nobody left to answer, or nothing parseable to answer to).
struct ReadResult {
    enum class Status {
        Ok,
        Closed,         // peer went away / read timed out / empty read
        HeaderTooLarge, // headers never ended within max_header_bytes → 431
        BodyTooLarge,   // declared Content-Length over max_body_bytes → 413
        LengthRequired, // Transfer-Encoding: chunked (we read by length only) → 411
    };
    Status status = Status::Closed;
    std::string data;
};

// Read a full request: headers, then Content-Length bytes of body, enforcing the
// server's limits while reading (an oversized request is refused as soon as its
// header block says so — not after buffering it).
ReadResult read_request(int fd, const HttpServer::Limits& limits) {
    ReadResult out;
    char buf[4096];
    std::size_t content_length = 0;
    bool have_headers = false;
    std::size_t header_end = 0;

    while (true) {
        ssize_t n = ::recv(fd, buf, sizeof(buf), 0);
        if (n <= 0) return out; // Closed (peer, error, or SO_RCVTIMEO expiry)
        out.data.append(buf, static_cast<std::size_t>(n));

        if (!have_headers) {
            auto pos = out.data.find("\r\n\r\n");
            if (pos == std::string::npos) {
                if (out.data.size() > limits.max_header_bytes) {
                    out.status = ReadResult::Status::HeaderTooLarge;
                    return out;
                }
                continue;
            }
            have_headers = true;
            header_end = pos + 4;
            std::string head = out.data.substr(0, pos);
            if (head.size() > limits.max_header_bytes) {
                out.status = ReadResult::Status::HeaderTooLarge;
                return out;
            }
            if (has_chunked_body(head)) {
                out.status = ReadResult::Status::LengthRequired;
                return out;
            }
            content_length = parse_content_length(head).value_or(0);
            if (content_length > limits.max_body_bytes) {
                out.status = ReadResult::Status::BodyTooLarge;
                return out;
            }
        }
        if (out.data.size() >= header_end + content_length) {
            out.status = ReadResult::Status::Ok;
            return out;
        }
    }
}

// True if the request is a WebSocket Upgrade (Upgrade: websocket + Connection
// listing "upgrade"). Pure string inspection — no OpenSSL, no framing.
bool is_websocket_upgrade(const Request& req) {
    const std::string* upgrade = nullptr;
    const std::string* connection = nullptr;
    for (const auto& kv : req.headers) {
        std::string name = lower(kv.first);
        if (name == "upgrade") upgrade = &kv.second;
        else if (name == "connection") connection = &kv.second;
    }
    if (!upgrade || !connection) return false;
    return lower(*upgrade).find("websocket") != std::string::npos &&
           lower(*connection).find("upgrade") != std::string::npos;
}

} // namespace

namespace {

void send_all(int fd, const std::string& out) {
    std::size_t sent = 0;
    while (sent < out.size()) {
        ssize_t n = ::send(fd, out.data() + sent, out.size() - sent, 0);
        if (n <= 0) break;
        sent += static_cast<std::size_t>(n);
    }
}

void set_recv_timeout(int fd, int seconds) {
    timeval tv{};
    tv.tv_sec = seconds; // 0 disables (blocking recv again)
    ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
}

} // namespace

void HttpServer::handle_client(int client, const std::string& remote_addr) {
    // The timeout guards the request read: a client that connects and trickles (or
    // sends nothing) releases this worker after limits_.read_timeout_seconds.
    if (limits_.read_timeout_seconds > 0) set_recv_timeout(client, limits_.read_timeout_seconds);

    ReadResult raw = read_request(client, limits_);
    switch (raw.status) {
        case ReadResult::Status::Closed:
            ::close(client);
            return;
        case ReadResult::Status::HeaderTooLarge:
            send_all(client, format_http_response(Response{431, "Request Header Fields Too Large"}));
            ::close(client);
            return;
        case ReadResult::Status::BodyTooLarge:
            send_all(client, format_http_response(Response{413, "Payload Too Large"}));
            ::close(client);
            return;
        case ReadResult::Status::LengthRequired:
            send_all(client, format_http_response(Response{411, "Length Required"}));
            ::close(client);
            return;
        case ReadResult::Status::Ok:
            break;
    }

    auto req = parse_http_request(raw.data);
    if (req) req->remote_addr = remote_addr;

    // WebSocket Upgrade: hand the live socket to the registered endpoint handler,
    // which owns it for the connection's lifetime (handshake + frame loop) and
    // closes it. If nothing is registered for the path, fall through to a 404.
    if (req && is_websocket_upgrade(*req)) {
        auto it = ws_handlers_.find(req->path);
        if (it != ws_handlers_.end()) {
            // The read timeout was for the HTTP request; a WebSocket connection is
            // long-lived and legitimately idle, so hand the socket over blocking.
            if (limits_.read_timeout_seconds > 0) set_recv_timeout(client, 0);
            it->second(client, *req);
            return;
        }
        // Upgrade requested but no endpoint registered for this path: fall through
        // to the normal request cycle (the router answers, typically a 404).
    }

    // Kernel already catches; this belt-and-braces catch covers other KernelContract
    // implementations so a throw can never escape into the worker thread and terminate.
    Response res;
    if (!req) {
        res = Response{400, "Bad Request"};
    } else {
        try {
            res = kernel_->handle(*req);
        } catch (const std::exception& e) {
            std::fprintf(stderr, "[http] unhandled exception: %s\n", e.what());
            res = Response{500, "Internal Server Error"};
        } catch (...) {
            std::fprintf(stderr, "[http] unhandled non-std exception\n");
            res = Response{500, "Internal Server Error"};
        }
    }
    send_all(client, format_http_response(res));
    ::close(client);
}

void HttpServer::accept_loop(int server_fd) {
    // Multiple worker threads may call accept() on the same listening socket; the
    // kernel hands each connection to exactly one of them. stop() closes the socket,
    // which makes accept() fail — the running_ check then breaks the loop.
    while (running_) {
        sockaddr_in peer{};
        socklen_t peer_len = sizeof(peer);
        int client = ::accept(server_fd, reinterpret_cast<sockaddr*>(&peer), &peer_len);
        if (client < 0) {
            if (!running_) break;
            continue;
        }
        char ip[INET_ADDRSTRLEN] = "";
        if (peer.sin_family == AF_INET)
            ::inet_ntop(AF_INET, &peer.sin_addr, ip, sizeof(ip));
        handle_client(client, ip);
    }
}

void HttpServer::stop() {
    running_ = false;
    int fd = server_fd_.exchange(-1);
    if (fd >= 0) {
        ::shutdown(fd, SHUT_RDWR); // wake any accept() blocked on this socket
        ::close(fd);
    }
}

int HttpServer::serve(int workers) {
    int server_fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) return 1;

    int opt = 1;
    ::setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(static_cast<uint16_t>(port_));

    if (::bind(server_fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        ::close(server_fd);
        return 1;
    }
    if (::listen(server_fd, 64) < 0) {
        ::close(server_fd);
        return 1;
    }

    if (workers < 1) workers = 1;
    std::printf("cpp-laravel serving on http://127.0.0.1:%d (%d worker%s, Ctrl-C to stop)\n",
                port_, workers, workers == 1 ? "" : "s");
    std::fflush(stdout);

    running_ = true;
    server_fd_ = server_fd;

    if (workers == 1) {
        accept_loop(server_fd);
    } else {
        std::vector<std::thread> pool;
        pool.reserve(static_cast<std::size_t>(workers));
        for (int i = 0; i < workers; ++i)
            pool.emplace_back([this, server_fd] { accept_loop(server_fd); });
        for (auto& t : pool) t.join();
    }

    // Close the listening socket if stop() didn't already (idempotent via exchange).
    int fd = server_fd_.exchange(-1);
    if (fd >= 0) ::close(fd);
    return 0;
}
