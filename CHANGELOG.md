# Changelog

Notable changes to cpp-laravel. Versioning is semantic; a passing test suite
(`ctest`) is the release gate. Depend on a tag and upgrade deliberately — see
[`docs/consuming.md`](docs/consuming.md).

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
