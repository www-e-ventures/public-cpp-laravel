# Changelog

Notable changes to cpp-laravel. Versioning is semantic; a passing test suite
(`ctest`) is the release gate. Depend on a tag and upgrade deliberately — see
[`docs/consuming.md`](docs/consuming.md).

## [0.7.0] — 2026-07-01

"The server survives" — robustness hardening for the HTTP and WebSocket layers.
Additive: new limits have permissive defaults; existing routes and consumers are
unaffected unless they were relying on a bug.

### Added
- **Exception containment.** A throw escaping a handler/middleware (a bad
  `std::stoi`, a container miss, a DB error) is now caught — in `Kernel::handle`
  and, belt-and-braces, in the server's dispatch — logged to stderr, and answered
  as a plain `500`. Previously it propagated out of the worker thread and
  `std::terminate`d the whole multi-worker server.
- **Request limits** — `HttpServer::Limits` (`set_limits()` before `serve()`):
  `max_header_bytes` (default 64 KiB → `431`), `max_body_bytes` (default 16 MiB,
  refused off the declared Content-Length before buffering → `413`), and
  `read_timeout_seconds` (default 30 s; a stalled/trickling read releases the
  worker; cleared before a WebSocket handler takes the socket).
- **`405 Method Not Allowed`.** A path registered under other verbs now answers
  `405` with an `Allow` header instead of a misleading `404`. New
  `RouterContract::allowed_methods(path)` (virtual with a default, so custom
  routers keep compiling) backs it.
- **`Request.remote_addr`** — the peer IP as accepted, for IP-keyed rate limiting
  and access logging. Empty off-socket (tests, FFI/WASM hosts).
- **WebSocket message-size cap.** `Connection::set_max_message_bytes()` (default
  1 MiB): a frame header declaring more closes with `1009` before the payload is
  ever buffered (previously an attacker-declared 64-bit length drove a
  `reserve()`/unbounded buffering); fragments that sum past the cap also `1009`.
  `parse_frame(buf, max_payload)` exposes the same check (`Frame.too_big`).
- **WebSocket protocol hygiene.** Unmasked client frames are rejected with `1002`
  per RFC 6455 §5.1 (`set_require_masked(false)` to opt out for server-to-server
  peers); a peer's Close status code is echoed back (was: always an empty Close);
  protocol-error closes now carry `1002`. `ws::encode_close(code)` added;
  `Frame.masked` exposed.
- **HTTP parsing correctness.** `Content-Length` is matched case-insensitively at
  header-line starts (`parse_content_length()`); `Transfer-Encoding: chunked` is
  answered with `411 Length Required` instead of hanging on a length that never
  comes (`has_chunked_body()`). New reason phrases: 400/405/411/413/431.
- **`check.sh`** — the release gate script: builds default AND Release under
  `-Wall -Wextra -Werror` and runs the suite in both. (An optimized GCC 13 build
  caught a real dangling-temporary in a `CHECK_EQ` on `repo.find(id)->title`; the
  harness macro now copies its operands, and Release is part of the gate so the
  class stays fixed.)

## [0.6.0] — 2026-07-01

WebSocket binary frames + a terminal bridge — a browser terminal (a CoCo/BBS-style
client) dialing in over WebSocket and rendering a byte stream. Additive.

### Added
- `ws::encode_binary` / `Connection::send_binary` / `Hub::broadcast_binary` — binary
  WebSocket frames, for raw byte streams (ANSI/CP437 output isn't valid UTF-8, so it
  can't ride in a text frame). Handles all RFC 6455 length forms.
- `ws::run_terminal(conn, on_input)` — pumps a connection as a byte terminal: forwards
  each inbound message to `on_input` (which can write back via `send_binary`) until the
  peer closes. A live backend can also push output from another thread.
- `examples/coco-web`: a `WS /terminal` endpoint + `public/terminal.html` — a minimal
  browser terminal over the WebSocket layer.

### Docs
- README (dev + public): new "HTTP, routing & static serving" and "Auth, sessions &
  tokens" sections covering the v0.2.0–v0.6.0 additions.

## [0.5.0] — 2026-07-01

### Added
- `examples/coco-web/` — a minimal reference app: a static SPA served by the framework
  alongside a JSON API over the ORM, with a token-gated write route. Exercises static
  serving (v0.3.0) and the identity toolkit (v0.4.0) end to end — the shape an outside
  app takes when it FetchContents the framework. Needs OpenSSL; skipped otherwise.

## [0.4.0] — 2026-07-01

Identity toolkit (opt-in, OpenSSL-gated) — stateless signed tokens and hashed
personal access tokens, both keyed on plain-string abilities. Additive; the core
stays dependency-free.

### Added
- `token.hpp` — stateless HMAC-SHA256 signed tokens (compact HS256 JWT):
  `tok::sign(Claims, secret)` / `tok::verify(token, secret[, now])`. Claims carry
  `sub`, `abilities` (plain strings), `iat`, `exp`; verify checks the signature
  (constant-time) and expiry. A backend that shares the secret verifies with no DB
  round-trip. `tok::requires_ability(ability, secret)` is route middleware that gates
  on a bearer token's abilities.
- `personal_access_token.hpp` — `pat::Store` over the ORM `Connection`: `issue()`
  returns the plaintext once and stores only its SHA-256 hash; `authenticate()`
  matches the hash and checks expiry; `revoke()` deletes by id. Records expose `can()`.
- Both sit in the OpenSSL-gated layer alongside `pbkdf2_hash.hpp`. Abilities are a
  single caller-defined vocabulary shared by signed tokens, PATs, and `gate.hpp`.

## [0.3.0] — 2026-07-01

Static file serving — the piece needed to serve a WASM/SPA app (and any assets)
alongside a JSON API. Additive; existing behavior unchanged.

### Added
- `static_files.hpp` — `staticfiles::serve(root, rel_path, req)` serves a file from a
  directory with: a MIME table (notably `.wasm → application/wasm`, required for
  `WebAssembly.instantiateStreaming`), byte-range requests (`Range` → `206`/`416`),
  `ETag` + `If-None-Match` → `304`, and `Cache-Control`. Bodies are byte-exact
  (binary-safe). Path traversal outside `root` is refused (`404`).
- `staticfiles::mount(router, "/dist", "web/dist")` — registers a catch-all static route.
- Router catch-all params: `{name*}` matches the rest of the path including `/`
  (e.g. `/dist/{path*}`). Plain `{name}` still matches a single segment.

## [0.2.0] — 2026-07-01

Additive HTTP correctness + ergonomics. No existing behavior or public API changed.

### Added
- `url_decode()` in `http.hpp`, and percent-decoding in `parse_form()`: form bodies
  now decode `%XX` escapes and treat `+` as a space (previously passed through raw).
  Cookie parsing is unchanged (taken verbatim).
- `Request.query` — the URL query string is parsed into a map (`/x?a=1&b=2`, url-decoded),
  while `Request.path` stays query-free.
- Router verbs `put()`, `patch()`, and `del()` (the DELETE verb; `delete` is a C++
  keyword). `add()` already accepted any method string; these are convenience sugar.
- `HttpServer::stop()` — closes the listening socket and breaks the accept loop so a
  server running on a background thread can shut down cleanly (e.g. on SIGINT/SIGTERM).

## [0.1.0-beta01]

First tagged release: the framework as first published — service container with
autowiring, providers/facades, routing + middleware, an Eloquent-style ORM (query
builder, relations, migrations, SQLite), Blade-lite views, Livewire-style components,
auth/session/gates/CSRF, cache/queue/mail/events/broadcasting, a threaded HTTP server,
WebSocket primitives, `cpp-artisan`, and the native / PHP-FFI / WASM host lifecycle.
