// static_files.hpp — serve files from a directory: MIME by extension, byte-range
// (206) requests, ETag / 304, and Cache-Control. Header-only, std-lib-only
// (<filesystem>) — no external dependency. Include it in the app that wants to
// serve a static SPA / assets next to its JSON API.
//
// Wiring: staticfiles::mount(router, "/dist", "web/dist") registers a catch-all
// GET route (needs the router's {param*} catch-all) that maps /dist/<rest> to a
// file under web/dist. Or call serve(root, rel, req) directly from any handler.
//
// Path traversal is refused: a resolved path that escapes `root` returns 404.
#pragma once
#include <cctype>
#include <filesystem>
#include <fstream>
#include <string>

#include "http.hpp"
#include "router_contract.hpp"

namespace staticfiles {

struct Options {
    std::string cache_control = "public, max-age=3600";
};

// Content-Type from a file extension. Browsers require application/wasm for
// WebAssembly.instantiateStreaming, so that one matters most here.
inline std::string mime_type(const std::string& path) {
    auto dot = path.rfind('.');
    std::string ext = dot == std::string::npos ? "" : path.substr(dot + 1);
    for (char& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (ext == "wasm") return "application/wasm";
    if (ext == "js" || ext == "mjs") return "application/javascript";
    if (ext == "html" || ext == "htm") return "text/html; charset=utf-8";
    if (ext == "css") return "text/css";
    if (ext == "json") return "application/json";
    if (ext == "svg") return "image/svg+xml";
    if (ext == "png") return "image/png";
    if (ext == "jpg" || ext == "jpeg") return "image/jpeg";
    if (ext == "gif") return "image/gif";
    if (ext == "ico") return "image/x-icon";
    if (ext == "woff2") return "font/woff2";
    if (ext == "woff") return "font/woff";
    if (ext == "wav") return "audio/wav";
    if (ext == "txt") return "text/plain; charset=utf-8";
    return "application/octet-stream";
}

// Case-insensitive header lookup (clients pick their own casing for Range etc.).
inline std::string header_ci(const Request& req, const std::string& name) {
    for (const auto& kv : req.headers) {
        if (kv.first.size() != name.size()) continue;
        bool eq = true;
        for (std::size_t i = 0; i < name.size(); ++i)
            if (std::tolower(static_cast<unsigned char>(kv.first[i])) !=
                std::tolower(static_cast<unsigned char>(name[i]))) { eq = false; break; }
        if (eq) return kv.second;
    }
    return "";
}

// Resolve root/rel, refusing anything that escapes root (traversal, absolute paths).
// Returns "" on rejection. Uses weakly_canonical so a non-existent tail still resolves.
inline std::string safe_resolve(const std::string& root, const std::string& rel) {
    namespace fs = std::filesystem;
    if (rel.find("..") != std::string::npos) return "";
    std::error_code ec;
    fs::path base = fs::weakly_canonical(fs::path(root), ec);
    fs::path full = fs::weakly_canonical(fs::path(root) / rel, ec);
    if (ec) return "";
    std::string bs = base.string(), ns = full.string();
    if (ns == bs) return ns;
    std::string sep(1, static_cast<char>(fs::path::preferred_separator));
    if (ns.rfind(bs + sep, 0) != 0) return ""; // full must sit under base/
    return ns;
}

namespace detail {
struct Range { bool present = false, satisfiable = false; long long start = 0, end = 0; };

// Parse "bytes=start-end", "bytes=start-", or "bytes=-suffix". `end` is inclusive.
inline Range parse_range(const std::string& header, long long size) {
    Range r;
    const std::string prefix = "bytes=";
    if (header.rfind(prefix, 0) != 0) return r;
    r.present = true;
    std::string spec = header.substr(prefix.size());
    auto dash = spec.find('-');
    if (dash == std::string::npos) return r;
    std::string a = spec.substr(0, dash), b = spec.substr(dash + 1);
    try {
        if (a.empty()) {                     // suffix: the last b bytes
            if (b.empty()) return r;
            long long n = std::stoll(b);
            if (n <= 0) return r;
            r.start = size > n ? size - n : 0;
            r.end = size - 1;
        } else {
            r.start = std::stoll(a);
            r.end = b.empty() ? size - 1 : std::stoll(b);
        }
    } catch (...) { return r; }
    if (r.end > size - 1) r.end = size - 1;
    if (r.start < 0 || r.start > r.end || r.start >= size) return r; // unsatisfiable
    r.satisfiable = true;
    return r;
}
} // namespace detail

// Serve root/rel_path. 404 if missing or escaping root; 304 on a matching
// If-None-Match; 206 (+ Content-Range) for a satisfiable Range, 416 for an
// unsatisfiable one; otherwise 200 with the whole file. Sets Content-Type,
// Cache-Control, ETag, and Accept-Ranges.
inline Response serve(const std::string& root, const std::string& rel_path, const Request& req,
                      const Options& opts = {}) {
    namespace fs = std::filesystem;
    std::string path = safe_resolve(root, rel_path);
    if (path.empty()) return Response{404, "Not Found"};
    std::error_code ec;
    if (!fs::is_regular_file(path, ec)) return Response{404, "Not Found"};

    long long size = static_cast<long long>(fs::file_size(path, ec));
    long long mtime =
        static_cast<long long>(fs::last_write_time(path, ec).time_since_epoch().count());
    std::string etag = "\"" + std::to_string(size) + "-" + std::to_string(mtime) + "\"";
    std::string ctype = mime_type(path);

    if (header_ci(req, "If-None-Match") == etag) {
        Response r;
        r.status = 304;
        r.headers = {{"ETag", etag}, {"Cache-Control", opts.cache_control}};
        return r;
    }

    std::string range = header_ci(req, "Range");
    if (!range.empty()) {
        detail::Range rg = detail::parse_range(range, size);
        if (rg.present && !rg.satisfiable) {
            Response r;
            r.status = 416;
            r.headers = {{"Content-Range", "bytes */" + std::to_string(size)}};
            return r;
        }
        if (rg.satisfiable) {
            long long len = rg.end - rg.start + 1;
            std::ifstream f(path, std::ios::binary);
            f.seekg(rg.start);
            std::string data(static_cast<std::size_t>(len), '\0');
            f.read(&data[0], len);
            Response r;
            r.status = 206;
            r.body = std::move(data);
            r.headers = {{"Content-Type", ctype},
                         {"Content-Range", "bytes " + std::to_string(rg.start) + "-" +
                                               std::to_string(rg.end) + "/" +
                                               std::to_string(size)},
                         {"Accept-Ranges", "bytes"},
                         {"Cache-Control", opts.cache_control},
                         {"ETag", etag}};
            return r;
        }
    }

    std::ifstream f(path, std::ios::binary);
    std::string data((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    Response r;
    r.status = 200;
    r.body = std::move(data);
    r.headers = {{"Content-Type", ctype},
                 {"Accept-Ranges", "bytes"},
                 {"Cache-Control", opts.cache_control},
                 {"ETag", etag}};
    return r;
}

// Convenience: register a catch-all GET route mapping `url_prefix/<rest>` to files
// under `dir`. Needs the router's {param*} catch-all (matches path separators).
inline void mount(RouterContract& router, const std::string& url_prefix, const std::string& dir,
                  Options opts = {}) {
    router.get(url_prefix + "/{path*}", [dir, opts](Request& req) {
        return serve(dir, req.route_params["path"], req, opts);
    });
}

} // namespace staticfiles
