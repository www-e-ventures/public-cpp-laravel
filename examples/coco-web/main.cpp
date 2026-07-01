// coco-web demo: serve the static SPA + JSON API. `./coco_web [port]` (default 8080).
#include <cstdio>
#include <cstdlib>
#include <string>

#include "app.hpp"
#include "http_server.hpp"
#include "websocket_server.hpp"

int main(int argc, char** argv) {
    int port = argc > 1 ? std::atoi(argv[1]) : 8080;

    // A real deployment reads the token secret from the environment; hardcoded here
    // only so the demo runs. Mint a token with the "items:write" ability to POST.
    const char* env_secret = std::getenv("COCO_WEB_SECRET");
    std::string secret = env_secret ? env_secret : "demo-secret-change-me";

    cocoweb::App app = cocoweb::bootstrap(COCO_WEB_PUBLIC_DIR, secret);

    std::printf("coco-web on http://127.0.0.1:%d\n", port);
    std::printf("  GET  /                    -> index.html (static)\n");
    std::printf("  GET  /assets/<file>       -> static asset (correct MIME, incl. .wasm)\n");
    std::printf("  GET  /api/items[?limit=N] -> JSON list (ORM)\n");
    std::printf("  POST /api/items           -> create (needs a token with 'items:write')\n");
    std::printf("  WS   /terminal            -> byte terminal (open /assets/terminal.html)\n");

    HttpServer server(app.kernel, port);

    // A WebSocket "terminal": greet, then echo each line the browser sends. Output goes
    // as BINARY frames — a real backend (a BBS) would stream ANSI/CP437 bytes here, which
    // aren't valid UTF-8 and so can't ride in a text frame. run_terminal pumps the input.
    server.on_websocket("/terminal", [](int fd, const Request& req) {
        ws::Connection conn(fd, req);
        if (!conn.handshake()) {
            conn.close();
            return;
        }
        conn.send_binary("coco-web terminal - type a line and press Enter.\r\n> ");
        ws::run_terminal(conn, [](ws::Connection& c, const std::string& line) {
            c.send_binary("you said: " + line + "\r\n> ");
        });
        conn.close();
    });

    return server.serve(4);
}
