# Changelog

Notable changes to cpp-laravel. Versioning is semantic; a passing test suite
(`ctest`) is the release gate. Depend on a tag and upgrade deliberately — see
[`docs/consuming.md`](docs/consuming.md).

## [0.10.0] — 2026-07-02

Docs truth pass + WebSocket polish. Small and additive.

### Added
- **`ws::Connection::set_allowed_origins({...})`** — an Origin allowlist on the
  handshake: a browser connection from an unlisted origin is answered `403` and the
  upgrade refused. WebSockets bypass CORS, so this is the cross-site-WebSocket-
  hijacking gate for endpoints whose auth rides on cookies. Empty (default) =
  accept any origin, as before.

### Changed
- **`ws::Hub` broadcasts snapshot-then-send** — the hub mutex is no longer held
  across every socket write, so one slow client can't stall the whole fan-out or
  block `add()`/`remove()` (which also removes a lock-order hazard against a
  connection's own send mutex). Lifetime contract documented on the class:
  `remove(c)` before tearing down `c`.

### Docs
- README brought current through v0.9.0: request limits + error semantics, the
  `httpauth` kit, queue + scheduler + new artisan commands, `Response::bytes`,
  static `extra_headers`, and the full header list in Layout. New "Integrating
  without the blocking server" section: what a reactor-based consumer reuses from
  the pure WS codec, and the reader-thread bridge from blocking `receive()` to a
  poll-per-tick byte stream. Honesty fixes: "soft deletes" is titled as the
  explicit pattern it is; the dangling `HANDOFF.md` reference now points at the
  changelog.

## [0.9.0] — 2026-07-01

"The promised features" — everything the consumers pre-negotiated in the wish-list
thread that hadn't shipped yet: the finished queue + a scheduler (the BBS's daily-turn
door games), binary responses + static extra headers (coco's save-states and future
threaded WASM), and ORM transactions. Additive except one fix called out below.

### Added
- **`Response::bytes(data, len, content_type)`** (+ a `std::string` overload) — the
  binary-blob response coco's integration sketch has been calling all along. Byte-
  exact (NULs and high bytes survive). `examples/coco-web` gains the reference
  save-state endpoints: `POST /api/savestates` (raw binary body, `saves:write`
  ability) and `GET /api/savestates/{id}` round-trip a snapshot byte-for-byte.
- **`staticfiles::Options.extra_headers`** — headers stamped on every static
  response (200/206/304/416). The driving recipe, documented on the struct: the
  `Cross-Origin-Opener-Policy: same-origin` + `Cross-Origin-Embedder-Policy:
  require-corp` pair a SharedArrayBuffer/threaded-WASM app needs on its HTML and
  its module.
- **The DB queue, finished.** Jobs carry `available_at` (delayed jobs: `push(job,
  payload, delay)`) and `reserved_at`; `work(now)` claims each due job with an
  atomic compare-and-set, so N workers — threads or separate processes on one
  database — never double-process (previously two workers both read and both ran
  every job). Failures back off (`retry_backoff * attempts`); jobs orphaned by a
  crashed worker are reclaimed after `retry_after` seconds; `now` is injectable so
  none of it needs sleeps to test. Table columns documented in the header.
- **`scheduler.hpp` — a tiny cron-style `Schedule`.** `every(seconds, name, fn)` and
  `daily_at("HH:MM", name, fn)` (local time; a late tick still runs today's task —
  the daily-turn shape), driven by `run_pending(now)` from real cron, a loop, or
  `schedule:run`. Last-run stamps persist through a Connection ("schedule_runs")
  so restarts don't double-fire; a throwing task doesn't re-fire every tick.
- **`Connection::update_if(table, id, guard_col, guard_value, row)`** — compare-and-
  set update (the queue's claim primitive). Atomic on both shipped backends; virtual
  with a read-compare-write default so custom backends keep compiling.
- **Transactions**: `Connection::begin/commit/rollback` (no-op defaults; SQLite maps
  to BEGIN/COMMIT/ROLLBACK, MemoryConnection snapshots/restores) and a
  `transaction(conn, fn)` helper — commit on return, rollback + rethrow on throw.
  Caveat documented: the shipped backends share one underlying connection across
  threads, so keep transactions short.
- **`cpp-artisan`**: `migrate`, `migrate:rollback` (Migrator::rollback existed but
  had no CLI), `queue:work [poll_seconds]` (SIGINT/SIGTERM-clean worker loop), and
  `schedule:run` (one tick, for cron). The blog example wires a demo handler +
  schedule and migrates the queue/scheduler tables.

### Fixed
- **`SqliteConnection::insert` throws on a refused INSERT** (constraint violation,
  missing table/column) instead of returning a bogus id 0 that read as success.

## [0.8.0] — 2026-07-01

"Identity done right" — security hardening across sessions, auth, tokens, and the
query builder. Mostly additive; two deliberate behavior changes are called out.

### Added
- **`http_auth.hpp` — the HTTP auth kit**, promoted out of the blog example so every
  consumer shares ONE audited implementation instead of copy-pasting (the BBS
  hand-rolled its own admin cookie sessions for exactly this reason). Zero-dep,
  header-only, in the `httpauth` namespace: `session_middleware` (cookie issue with
  configurable `HttpOnly`/`Secure`/`SameSite` flags via `CookieOptions`),
  `csrf_token` + `verify_csrf` (CSPRNG tokens, constant-time compare, 419),
  `require_auth` (401), `attempt_login` (regenerates the session id on success —
  session-fixation defense — and stamps the new cookie), `logout` (full session
  flush), and `throttle` (fixed-window rate limiter keyed on `Request.remote_addr`
  with cookie fallback; the window boundary lives in the cached value, fixing the
  example limiter that reset its TTL on every hit and never unblocked sustained
  traffic).
- **CSPRNG ids** — `csprng_hex(n)` (/dev/urandom) in `session.hpp`;
  `new_session_id()` now returns 128 unpredictable bits (was: a seeded
  `mt19937_64` stream — observable, predictable) and CSRF tokens mint from the
  same source.
- **Session TTL + GC + regeneration** — `ArraySessionStore(ttl)` expires idle
  sessions (lazily on access, in bulk via `gc()`); `SessionStore::rename()`
  (virtual, default "unsupported") backs the new `Session::regenerate_id()`;
  `Guard::session()` exposes the handle the HTTP layer needs.
- **Injectable password hasher** — `ArrayUserProvider(std::shared_ptr<const
  HashContract>)`: inject `Pbkdf2Hasher` for production passwords; the dep-free
  FNV `Hasher` stays the default demo. (The stale "passwords are compared in
  plain text" comment in auth.hpp is gone — it hadn't been true for a while.)
- **SQL identifier guard** — `is_sql_identifier()` in database.hpp;
  `QueryBuilder::where/where_in/order_by` and every spliced name in
  `SqliteConnection` refuse non-identifier column/table names
  (`std::invalid_argument`). Closes the `?sort=views;DROP TABLE` splice — values
  were always bound; identifiers now can't smuggle SQL either.
- `302 Found` / `301 Moved Permanently` reason phrases.

### Changed (behavior)
- **`ArraySessionStore` is now mutex-guarded** — it was shared across HttpServer
  worker threads without one (a real data race under `serve(N > 1)`).
- **Successful login via the kit regenerates the session id**: the old (possibly
  attacker-planted) id stops authenticating and the response carries the new
  cookie. Clients must follow `Set-Cookie` — browsers already do; test clients
  may need updating (the blog feature tests show the pattern).
- **`pat::Store::authenticate` looks up by `WHERE token_hash = ?`** instead of
  scanning every row — O(index) as tokens accumulate. Add a UNIQUE index on
  `token_hash` in your migration (see the header comment).
- The blog example's `session_middleware`/`verify_csrf`/`require_auth`/`throttle`
  free functions moved into the framework as `httpauth::*`; `examples/blog` now
  wires the kit (its local `throttle.hpp` is gone).

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
