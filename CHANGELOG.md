# Changelog

Notable changes to cpp-laravel. Versioning is semantic; a passing test suite
(`ctest`) is the release gate. Depend on a tag and upgrade deliberately — see
[`docs/consuming.md`](docs/consuming.md).

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
