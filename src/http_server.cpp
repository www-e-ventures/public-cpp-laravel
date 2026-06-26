// http_server.cpp — see http_server.hpp
#include "http_server.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

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
        case 401: return "Unauthorized";
        case 403: return "Forbidden";
        case 404: return "Not Found";
        case 422: return "Unprocessable Entity";
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

} // namespace

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
    req.path = target.substr(0, target.find('?')); // drop query string (slice scope)
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

// Read a full request: headers, then Content-Length bytes of body.
std::string read_request(int fd) {
    std::string data;
    char buf[4096];
    std::size_t content_length = 0;
    bool have_headers = false;
    std::size_t header_end = 0;

    while (true) {
        ssize_t n = ::recv(fd, buf, sizeof(buf), 0);
        if (n <= 0) break;
        data.append(buf, static_cast<std::size_t>(n));

        if (!have_headers) {
            auto pos = data.find("\r\n\r\n");
            if (pos != std::string::npos) {
                have_headers = true;
                header_end = pos + 4;
                // crude Content-Length scan (case-insensitive-ish, common casing).
                auto cl = data.find("Content-Length:");
                if (cl == std::string::npos) cl = data.find("content-length:");
                if (cl != std::string::npos && cl < pos) {
                    auto eol = data.find("\r\n", cl);
                    content_length = static_cast<std::size_t>(
                        std::strtoul(data.substr(cl + 15, eol - cl - 15).c_str(), nullptr, 10));
                }
            }
        }
        if (have_headers && data.size() >= header_end + content_length) break;
    }
    return data;
}

} // namespace

void HttpServer::handle_client(int client) {
    std::string raw = read_request(client);
    auto req = parse_http_request(raw);
    Response res = req ? kernel_->handle(*req) : Response{400, "Bad Request"};
    std::string out = format_http_response(res);

    std::size_t sent = 0;
    while (sent < out.size()) {
        ssize_t n = ::send(client, out.data() + sent, out.size() - sent, 0);
        if (n <= 0) break;
        sent += static_cast<std::size_t>(n);
    }
    ::close(client);
}

void HttpServer::accept_loop(int server_fd) {
    // Multiple worker threads may call accept() on the same listening socket; the
    // kernel hands each connection to exactly one of them.
    while (true) {
        int client = ::accept(server_fd, nullptr, nullptr);
        if (client < 0) continue;
        handle_client(client);
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

    if (workers == 1) {
        accept_loop(server_fd);
        return 0;
    }

    std::vector<std::thread> pool;
    pool.reserve(static_cast<std::size_t>(workers));
    for (int i = 0; i < workers; ++i)
        pool.emplace_back([this, server_fd] { accept_loop(server_fd); });
    for (auto& t : pool) t.join();
    return 0;
}
