# coco-web — a reference app: static SPA + JSON API

A minimal app built ON cpp-laravel that serves a static single-page app **and** a
JSON API from the same compiled binary. It's the shape an outside project takes when
it depends on the framework (e.g. via `FetchContent` — see `docs/consuming.md`), and
it exercises two framework features end to end:

- **Static file serving** (`static_files.hpp`) — `/` serves `public/index.html`,
  `/assets/<file>` serves assets with correct MIME types (including
  `application/wasm`, needed for `WebAssembly.instantiateStreaming`).
- **Token-gated writes** (`token.hpp`) — `POST /api/items` requires a signed token
  carrying the `items:write` ability; `tok::requires_ability` verifies it with a
  shared secret and no database lookup.
- **WebSocket terminal** (`websocket_server.hpp`) — `WS /terminal` streams a byte
  terminal to the browser (binary frames) and echoes input; open
  `/assets/terminal.html`. This is the shape a retro client (a CoCo terminal dialing
  into a BBS) takes — the backend would push ANSI/CP437 bytes here.

Plus a small ORM-backed JSON API (`GET /api/items`, with `?limit=N`).

## Run it

```sh
cmake -S . -B build && cmake --build build
./build/examples/coco-web/coco_web 8080        # needs OpenSSL
# GET  http://127.0.0.1:8080/                 -> the SPA
# GET  http://127.0.0.1:8080/api/items        -> [{"id":1,"name":"first"}, ...]
# GET  http://127.0.0.1:8080/api/items?limit=1
```

`POST /api/items` needs a token. Mint one in code with `tok::sign(claims, secret)`
where `claims.abilities` includes `items:write`, then send it as
`Authorization: Bearer <token>`. The demo's secret is `COCO_WEB_SECRET` (env) or a
built-in default.

## Notes

- Uses the in-memory ORM backend (`MemoryConnection`); a real app binds SQLite via
  the same `Connection` contract.
- The emulator/WASM payload itself is not included here — this is the *server* shape.
  A real deployment drops a built `.wasm` into `public/assets/` and the SPA loads it.
