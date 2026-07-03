# ⚡ Laravel-in-C++

![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)
![C++20](https://img.shields.io/badge/C%2B%2B-20-00599C.svg)
![runs: native · WASM · PHP-FFI](https://img.shields.io/badge/runs-native%20%C2%B7%20WASM%20%C2%B7%20PHP--FFI-2ea44f.svg)

**The Laravel request lifecycle — compiled.** A Laravel-shaped web framework in modern C++20:
a service container with autowiring, service providers, facades, routing + middleware, an
Eloquent-style ORM (query builder, relationships, migrations, real SQLite), Blade templates,
Livewire-style reactive components, auth/session/gates/CSRF plus signed tokens, static file serving,
cache/queue/mail/events/broadcasting, a threaded HTTP server, and a `cpp-artisan` CLI — all as
compiled C++.

The request lifecycle sits behind a single C entry point, so the *same* compiled code runs three
ways: **natively**, from **PHP via FFI**, and **in your browser as WebAssembly**.

Beyond the demo app in this repo, the framework has a real consumer: a 1990s-style bulletin board
system (BBS) that serves a single ANSI/CP437 byte stream over telnet, the web, dial-up modem, and
LoRa radio. It vendors this core and drives it through the same lifecycle.

> **~390,000 requests/sec.** A quick node benchmark of `GET /articles/1` running as WebAssembly —
> 100k–500k iterations, single machine, and that figure *includes* the JS↔WASM call overhead.
> Native is faster; the point is that the whole router → middleware → container → ORM → JSON path
> is compiled, not interpreted.

## Try it

```sh
cmake -S . -B build && cmake --build build
./build/examples/blog/cpp-artisan serve 8080
curl -s localhost:8080/articles
```

Or open [`wasm/index.html`](wasm/) to drive the framework **entirely in the browser** — issue
requests, render a Blade view, and hit the benchmark button, with no server and no network. The
[`ffi/`](ffi/) folder runs the same lifecycle from PHP. Here's what a session looks like:

<!-- A screen capture of the WASM demo can go at docs/demo.gif and be referenced here. -->

```
GET  /articles       -> 200  [{"id":1,"title":"Hello from compiled C++", ...}]
POST /articles       -> 201  {"id":3,"title":"From the browser", ...}
GET  /articles.html  -> 200  <ul><li>Hello from compiled C++</li> ...</ul>   (Blade)
POST /articles       -> 422  {"error":"title is required"}                   (validation)
GET  /nope           -> 404  Not Found
```

## One lifecycle, many hosts

The request lifecycle is plain C++ and is reached through a single C entry point —
`extern "C" const char* lipp_handle(method, path, body)`. That C ABI is host-neutral: the same
compiled logic runs natively, in PHP, in the browser, or under a WASM runtime, with no change to
the framework.

```
                ┌──────────────────────────────────────────────────────────┐
                │      request lifecycle (compiled C++ — the framework)      │
                │  router → middleware → container → controller → ORM → Blade │
                └──────────────────────────────────────────────────────────┘
                                         │   extern "C"
                            lipp_handle(method, path, body)        ← host-neutral C ABI
                                         │
   ┌──────────────┬─────────────────────┼──────────────────────┬──────────────────────┐
   ▼              ▼                     ▼                      ▼                      ▼
 native C++    PHP via FFI           browser (WASM,        C++ host embedding      any C-FFI host
 (link the     → liblipp.so          thin JS shell)        a WASM runtime          (Python/Rust/Go…)
  library;     `ffi/`                `wasm/`               `wasm/host`             via the same .so
  `cpp-artisan`)                                            calls the .wasm
```

- **Native C++** — just link the framework. `cpp-artisan request GET /articles` already *is* the
  lifecycle in compiled C++ — no JS, no WASM.
- **PHP (`ffi/`)** — `FFI::cdef` loads `liblipp.so` and calls `lipp_handle` directly. No WASM, no JS.
- **Browser (`wasm/`)** — Emscripten compiles to WebAssembly; a thin (mostly generated) JS shell
  instantiates it. The request never leaves the page — a client-side speed demo (~390k req/s in a
  node benchmark).
- **C++ host of WASM (`wasm/host`)** — embeds the Wasmtime runtime and calls the `.wasm` exports
  from C++: C++ running C++-compiled-to-WASM, zero JS.

Server-side you usually wouldn't reach for WASM at all — native link (C++) or FFI (PHP) is simpler
and faster; WASM's payoff is browser execution or a portable sandbox.

## What this is (and isn't)

It reimplements Laravel's core runtime conventions in C++20 — **replicating functionality, not
transpiling PHP**. The rough split: ~70% of Laravel is ordinary OOP that ports cleanly; the other
~30% is runtime metaprogramming (reflection-driven DI, magic methods, facades) that has to be
codegen'd or consciously redesigned. So this builds deliberate C++ equivalents of Laravel's
*contracts and lifecycle*, not a PHP interpreter.

Three reasons it's interesting:

1. **Security surface** — no source on disk, no eval/include path, dispatch table fixed at link
   time. The injection/deserialization gadgets that plague interpreted stacks have no surface here.
2. **Speed** — a compiled request lifecycle, no per-request bytecode interpretation.
3. **Learning** — pressure-testing how far Laravel's DX survives translation.

It's a curiosity project first: there's no CI, and clarity plus honest trade-off documentation
matter more than feature completeness — though it does have real consumers now (next section).
The demo credentials (`ada`/`secret`, `secret-token`) are exactly that — demo values.

## In the wild

Two independent applications run on this framework, with deliberately opposite architectures —
which is the real test of the boundaries:

- **A 1990s-style bulletin board system** (telnet / web / dial-up modem / LoRa radio, one
  ANSI/CP437 byte stream) runs its interactive transports on its own epoll + C++20-coroutine
  reactor and **vendors the framework core** (`add_subdirectory`, offline builds) for everything
  request-shaped: the sysop dashboard and JSON API on `HttpServer`, the ORM + migrations, and the
  whole identity surface — hashed personal access tokens, HS256 "room" tokens another backend
  verifies with no shared database, admin sessions on `http_auth.hpp`, and daily-turn door games
  on the scheduler.
- **A Tandy Color Computer 3/4 emulator** compiles to WebAssembly with Emscripten and is **served
  by the framework** (FetchContent of a version tag): the multi-MB `.wasm` over static serving
  (correct MIME, `Range`, `ETag`), a JSON API, and versioned binary save-states over
  `Response::bytes`.

The vendoring consumer has upgraded across five consecutive releases (v0.5.0 → v0.10.0) with
**zero source changes** — the additive-semver contract holding in practice, not just in the
changelog. The two apps also meet in the middle: the BBS exposes a `/terminal` WebSocket endpoint
streaming raw CP437/ANSI in binary frames, and the emulator's terminal speaks it through a
byte-stream bridge — a genuine CoCo caller dialing a genuine BBS from a browser. (Both apps live
in private repositories today; links will appear here when they open up.)

## Layout

The repo is split into the **framework** (reusable) and an **example app** that builds on it:

```
include/        framework public headers
  http.hpp        Request / Response               (Illuminate\Http)
  *_contract.hpp  boundary interfaces (container/router/kernel) (Illuminate\Contracts\*)
  container.hpp   type-erased service container     (Illuminate\Container)
  pipeline.hpp    middleware pipeline               (Illuminate\Pipeline)
  router.hpp      {param} routing over std::regex   (Illuminate\Routing)
  kernel.hpp      request lifecycle wiring          (Illuminate\Foundation\Http\Kernel)
  service_provider.hpp  two-phase register/boot     (Illuminate\Support\ServiceProvider)
  facade.hpp      static proxy over the container   (Illuminate\Support\Facades\Facade)
  database.hpp    Connection contract + in-memory backend  (Illuminate\Database)
  repository.hpp  generic typed Repository<T, Mapper>      (Eloquent, repository-style)
  relations.hpp   has_many / has_one / belongs_to          (Eloquent relationships)
  schema.hpp      Blueprint + Schema::create               (Schema builder)
  migration.hpp   Migration + Migrator (idempotent)        (Illuminate\Database\Migrations)
  sqlite_connection.hpp  real SQL backend (optional)       (Illuminate\Database\SQLiteConnection)
  config.hpp      config store + .env parsing              (Illuminate\Config + env())
  validation.hpp  rule-based request validation            (Illuminate\Validation)
  events.hpp      type-erased event dispatcher             (Illuminate\Events)
  cache.hpp       cache contract + array store             (Illuminate\Contracts\Cache)
  queue.hpp       queue contract + sync/array drivers      (Illuminate\Contracts\Queue)
  db_queue.hpp    database queue driver (atomic claims)    (the "database" queue driver)
  scheduler.hpp   cron-style Schedule (every / daily_at)   (Illuminate\Console\Scheduling)
  mail.hpp        mailer contract + array driver           (Illuminate\Contracts\Mail)
  session.hpp     session store + handle (CSPRNG ids)      (Illuminate\Session)
  auth.hpp        session guard + user provider            (Illuminate\Auth\SessionGuard)
  http_auth.hpp   session cookies / CSRF / login / throttle (StartSession + VerifyCsrfToken + ...)
  gate.hpp        authorization gates                      (Illuminate\Auth\Access\Gate)
  pbkdf2_hash.hpp PBKDF2 password hasher (OpenSSL-gated)   (Hash::make, production-grade)
  token.hpp       HS256 signed tokens + ability middleware (OpenSSL-gated; JWT-ish)
  personal_access_token.hpp  hashed-at-rest API tokens     (OpenSSL-gated; Sanctum-lite)
  view.hpp        Blade-lite engine + Views registry       (Illuminate\View / Blade)
  livewire.hpp    server-side reactive components          (Livewire)
  testing.hpp     Laravel-flavoured HTTP test DSL          (Illuminate\Testing)
  http_server.hpp minimal HTTP/1.1 server (thread pool, limits) (the dev server)
  static_files.hpp  MIME / Range / ETag / extra headers    (static file serving)
  websocket.hpp   pure RFC 6455 codec (frames, accept key) (OpenSSL-gated)
  websocket_server.hpp  Connection / Hub / run_terminal    (OpenSSL-gated socket layer)
  test_harness.hpp / generators.hpp   test harness + make:* scaffolding templates
src/            framework implementations  → `framework` static lib (zero-dep)
                + sqlite_connection.cpp    → `framework_sqlite` lib (optional)
tools/          dev codegen tools: autowire/ (DI) and codegen/ (model mappers), libclang
tests/          FRAMEWORK unit tests (link only the framework)
examples/blog/  the demo APP built ON the framework — your project's shape:
  app/            models/, controllers, providers, facades, bootstrap, migrations
  main.cpp        demo executable          artisan.cpp   cpp-artisan CLI
  tests/          app feature/integration tests (Laravel-flavoured DSL)
  CMakeLists.txt  links the `framework` target
parity/         optional PHP/phpunit behavioural parity oracle
slice.cpp       original single-file prototype, kept for reference (not built)
```

Everything under `tests/`, `tools/`, and `examples/` builds only when the framework is the
top-level project (`PROJECT_IS_TOP_LEVEL`); a consumer pulling it in via `add_subdirectory` /
FetchContent gets just the `framework` (and `framework_sqlite`) libraries.

## Build & run

```sh
cmake -S . -B build
cmake --build build
./build/examples/blog/demo                    # run the demo app
ctest --test-dir build --output-on-failure    # framework + autowire + codegen + blog suites
```

Binaries live with their project: framework test runner at `build/framework_tests`; the app's
`demo`, `cpp-artisan`, and `blog_tests` under `build/examples/blog/`. Everything compiles under
`-Wall -Wextra -Werror` (a working agreement) on C++20.

### cpp-artisan (console CLI)

`cpp-artisan` mirrors `php artisan` — a console entry point that boots the app and drives it:

```sh
./build/examples/blog/cpp-artisan route:list
./build/examples/blog/cpp-artisan request GET /articles
./build/examples/blog/cpp-artisan request POST /articles "title=Hi&published=1" --token secret-token
```

`request` is a server-less way to exercise the kernel (tinker + curl in one). For a real listener:

```sh
./build/examples/blog/cpp-artisan serve 8080        # single-threaded
./build/examples/blog/cpp-artisan serve 8080 4      # 4 worker threads
curl -s localhost:8080/articles
curl -s -X POST -H "Authorization: secret-token" -d "title=Hi&published=1" localhost:8080/articles
```

Background work has its own commands (see "Queue & scheduler" below). `LIPP_DB=/path/app.sqlite`
points them all at a persistent database:

```sh
./build/examples/blog/cpp-artisan migrate           # apply migrations (idempotent)
./build/examples/blog/cpp-artisan migrate:rollback  # undo them
./build/examples/blog/cpp-artisan schedule:run      # one scheduler tick — call from cron
./build/examples/blog/cpp-artisan queue:work 1      # run queued jobs, poll every 1s, Ctrl-C stops
```

`http_server.hpp` bridges sockets to the Kernel; its request parsing / response formatting are pure
functions unit-tested in `tests/test_http_server.cpp`. With `workers > 1` it runs a **thread pool**
(workers accept on the shared socket) — safe because the **container and connections are
thread-safe** (recursive-mutex-guarded resolution; mutex-guarded backends; see
`tests/test_concurrency.cpp`). It's still a dev server (`Connection: close`), but it handles
concurrent requests correctly — verified by 40 concurrent POSTs across 4 workers landing exactly 40
rows in a shared SQLite file. For real persistence across workers, point `$LIPP_DB` at a file.

Scaffolding generators (template-based; rendering is unit-tested in `tests/test_generators.cpp`):

```sh
./build/examples/blog/cpp-artisan make:model Product          # -> app/models/product.hpp (struct + mapper + repo)
./build/examples/blog/cpp-artisan make:controller Order       # -> app/order_controller.hpp (REST skeleton)
```

`make:model` is the quick-start (one model); `tools/codegen/genmodel` is the bulk generator that
derives mappers from existing structs.

### Starting a new project (`cpp-artisan new`)

Scaffold a standalone app that builds *on* the framework (its `CMakeLists.txt` pulls the framework
in via `FetchContent`):

```sh
./build/examples/blog/cpp-artisan new myblog        # creates ./myblog (a Post CRUD starter)
cd myblog && cmake -S . -B build && cmake --build build && ctest --test-dir build
```

The generated project mirrors `examples/blog/`: `app/` (a `Post` model+mapper+repo, a controller,
`bootstrap`), `main.cpp`, and a feature test using the Laravel-flavoured DSL. To build against a
local framework checkout instead of fetching, pass `-DLIPP_SOURCE_DIR=/path/to/cpp-laravel` — the
framework's `PROJECT_IS_TOP_LEVEL` guard means only its libraries build, not its tests/tools/example.

### Installing the framework (`find_package`)

For consumers that prefer an installed package over FetchContent:

```sh
cmake -S . -B build && cmake --build build
cmake --install build --prefix /usr/local       # installs headers + libs + cmake config
```

Then in another project:

```cmake
find_package(lipp REQUIRED)
target_link_libraries(myapp PRIVATE lipp::framework)   # + lipp::framework_sqlite if installed
```

## Testing

There are two layers of tests. **Unit tests** cover each piece in isolation (container, autowiring,
providers, facades, ORM). **Feature / smoke tests** (`tests/test_feature_blog.cpp`) drive the whole
stack the way a client would and assert on the `Response` (`examples/blog/tests/test_feature_blog.cpp`).
The smoke-test app is a small Articles REST resource (`examples/blog/app/article_controller.hpp`,
routes in `examples/blog/app/bootstrap.cpp`): `GET /articles`,
`GET /articles/{id}`, `POST /articles` (auth-guarded, with validation). Each feature test boots a
fresh app (and a fresh in-memory DB), so they're independent.

Feature tests use a **Laravel/Pest-flavoured HTTP DSL** (`include/testing.hpp`, akin to
`Illuminate\Testing\TestResponse`) so they read like Laravel feature tests — at zero runtime cost,
since it's header-only test code:

```cpp
auto app = bootstrap();
HttpClient http(*app.kernel, __fails);

http.get("/articles").assertOk().assertExactBody("[]");
http.withToken("secret-token")
    .post("/articles", "title=Hello&published=1")
    .assertCreated().assertSee("Hello");
```

### Parity oracle (optional, PHP)

`parity/` proves the C++ app is **behaviourally faithful** to a real PHP stack: one phpunit suite
(Laravel/`Illuminate\Testing` style) runs against both the C++ server (`cpp-artisan serve`) and a
vanilla-PHP reference app with the same Articles routes — and must pass identically.

```sh
cd parity && composer install && cd ..
./parity/run-parity.sh
# -> PARITY OK — the same phpunit suite passes against both the C++ app and the PHP reference.
```

This is the only part needing a PHP/Composer toolchain; `ctest` remains the primary suite. See
`parity/README.md`.

A tiny dependency-free harness (`include/test_harness.hpp`): tests self-register with `TEST(name)`,
assert with `CHECK` / `CHECK_EQ`, and the runner prints an `N passing` summary and a non-zero exit
code on any failure. The five scenarios the original slice proved in its `main()` are ported here
so we never regress them (authorised+found, authorised+missing, missing-token short-circuit,
unrouted 404, singleton identity), plus focused tests for the autowiring helpers in
`tests/test_autowire.cpp`. With `libclang-dev` present, `ctest` also runs the `autowire` codegen
round-trip (see the autowiring section below).

## Views (Blade-lite) & cpp-livewire

`view.hpp` is a Blade-lite engine: `{{ }}` (escaped) / `{!! !!}` (raw), `@if/@else/@endif`,
`@foreach`, plus composition — `@include('partial')` and layout inheritance
(`@extends('layout')` + `@section`/`@endsection` filling the layout's `@yield`), resolved through a
`Views` registry of named templates. The demo renders `GET /articles.html` with it.

`livewire.hpp` brings **server-side reactive components** (à la Livewire). A `Component` has
string-keyed `state` and named `actions`; the protocol is hydrate → act → re-render:

```
browser holds rendered HTML + serialized state
  └─ wire:click fires → POST /livewire {name, action, arg, state}
       └─ server rehydrates the component from that state, runs the action,
          re-renders the Blade view, returns {html, state}
            └─ JS client swaps innerHTML + stores the new state
```

The example wires a counter: `GET /counter` mounts it, `POST /livewire` is the AJAX endpoint
(`Livewire::handle`), and `GET /livewire.js` serves a ~30-line client. Try it:

```sh
./build/examples/blog/cpp-artisan serve 8080 && open http://localhost:8080/counter
```

The client supports **`wire:click`** (run an action) and **`wire:model`** (two-way bind an input:
typing folds the value into state and posts a `$refresh`). Responses are applied by **DOM morphing**
(patch in place), not `innerHTML` replacement, so the focused input keeps its cursor while the rest
of the view updates. Hydration **merges** posted state over the component's defaults, so a request
may send only the properties it knows.

The server half is fully unit-tested (`tests/test_livewire.cpp`, `examples/blog/tests/test_feature_livewire.cpp`);
the JS client is exercised in the browser. Next for Livewire: nested components and lifecycle hooks.

## HTTP, routing & static serving

`HttpServer` is a threaded, blocking HTTP/1.1 server (`Connection: close`) that bridges real sockets
to the Kernel: read a request, `Kernel::handle` it, write the response — sized for an admin
dashboard or a JSON API behind a reverse proxy, not a hostile internet edge. `HttpServer::stop()`
closes the listening socket and breaks the accept loop, so a server running on a background thread
shuts down cleanly on SIGINT/SIGTERM.

The server survives misbehaviour: an exception escaping a handler or middleware becomes a plain
`500` (logged to stderr) instead of taking down the process; `HttpServer::Limits` caps header size
(`431`), body size (`413`, refused off the declared `Content-Length` before buffering), and stalls
a read timeout; `Transfer-Encoding: chunked` is answered `411` rather than hung on. A path
registered under other verbs answers `405` with an `Allow` header, and `Request.remote_addr`
carries the peer IP for rate limiting and logging. Binary responses (save-states, downloads) are
`Response::bytes(data, len, content_type)` — bodies are byte-exact in both directions.

`Router` registers handlers with `get`/`post`/`put`/`patch`/`del` (`del`, because `delete` is a
keyword); `add()` accepts any method string. Path params compile to a regex: `{id}` matches one
segment, `{path*}` is a catch-all that matches `/` too (for static prefixes). The query string is
parsed into `Request.query`, and both form bodies and query strings are percent-decoded (`%XX`, and
`+` → space) via `url_decode` / `parse_form`.

Static files (`static_files.hpp`, header-only, std-lib-only): `staticfiles::serve(root, rel, req)`
serves a file with a MIME table (notably `.wasm → application/wasm`, which browsers require for
`WebAssembly.instantiateStreaming`), byte-range requests (`Range` → 206/416), `ETag` +
`If-None-Match` → 304, and `Cache-Control`. Bodies are byte-exact; paths that escape `root` are
refused. `staticfiles::mount(router, "/dist", dir)` wires a catch-all static route, and
`Options.extra_headers` stamps custom headers on every response — the documented recipe is the
`Cross-Origin-Opener-Policy` + `Cross-Origin-Embedder-Policy` pair a `SharedArrayBuffer`/threaded
WASM app needs on both its HTML and its module. `examples/coco-web/` puts it together — a static
single-page app served next to a JSON API (including binary save-state endpoints).

WebSocket (`websocket.hpp` / `websocket_server.hpp`, OpenSSL-gated for the SHA-1 accept key):
`HttpServer::on_websocket(path, handler)` hands an Upgrade request's raw socket to a handler;
`ws::Connection` does the RFC 6455 handshake and per-connection frame loop (text/binary,
ping/pong/close with the peer's status code echoed, and reassembly of fragmented messages), and
`ws::Hub` broadcasts to every open connection (snapshot-then-send, so one slow client doesn't
stall the fan-out). Protocol hygiene is enforced: a frame header declaring more than the
per-connection message cap closes with `1009` before the payload is ever buffered, unmasked
client frames are refused with `1002` (RFC 6455 §5.1; `set_require_masked(false)` for
server-to-server peers), and `set_allowed_origins({...})` rejects cross-site browser connections
with a `403` when the endpoint's auth rides on cookies. The framework core stays OpenSSL-free —
the handler opts into the crypto.

**Integrating without the blocking server.** `ws::Connection` parks a thread per connection —
right for the dev server, wrong for an app with its own event loop. A reactor-based consumer
(epoll, coroutines) can skip it and reuse the *pure* layer directly: `ws::accept_key` for the
handshake, `ws::parse_frame` for opcode-aware decoding off its own buffers (including
fragmentation reassembly state and the declared-length cap), and `ws::encode_text` /
`encode_binary` / `encode_close(code)` / `encode_pong` for output — no threads, no sockets, no
opinions. One duty comes with that freedom: the framework can only turn exceptions into `500`s
for work that runs *inside* it — a reactor needs its own containment at the detached-coroutine
boundary, or one throwing session takes down the daemon (catch, log, let the one session die).
Conversely, an app whose I/O is a poll-per-tick byte stream (an emulator's serial port)
bridges to the blocking layer with a reader thread: `receive()` drains whole messages into a byte
queue the tick loop pops one byte at a time, and per-tick output is coalesced into a single
`send_binary()` (sends are mutex-serialised, so pushing from the tick thread while the reader is
parked in `receive()` is safe).

## Auth, sessions & tokens

Session auth is a `SessionGuard` over a session store (`auth.hpp`, `session.hpp`); authorization is
gates and policies (`gate.hpp`) — named abilities checked with `allows()`/`denies()`, with a
`before()` hook for admin overrides. Session ids (and CSRF tokens) are CSPRNG-generated (128 bits
from `/dev/urandom`); the in-memory store is mutex-guarded, supports an idle TTL with `gc()`, and
`Session::regenerate_id()` moves a session under a fresh id — the session-fixation defense. The
password hasher is injectable: the dependency-free default demonstrates the salt+verify shape, and
`Pbkdf2Hasher` (OpenSSL-gated) drops in behind the same `HashContract` for production passwords.

The HTTP wiring for all of this is `http_auth.hpp` (zero-dep, header-only, `httpauth::`): a
session-cookie middleware with configurable `HttpOnly`/`Secure`/`SameSite` flags, CSRF mint +
verify (constant-time compare, `419`), a `require_auth` route guard (`401`), `attempt_login`
(regenerates the session id on success and re-issues the cookie), `logout` (full session flush),
and a fixed-window rate limiter keyed on the client IP. One audited copy instead of a per-app
re-implementation — `examples/blog` wires it end to end.

For APIs and cross-service identity there's an optional, OpenSSL-gated token layer (next to the
PBKDF2 hasher; the core stays dependency-free):

- **Stateless signed tokens** (`token.hpp`): `tok::sign(claims, secret)` / `tok::verify(token,
  secret)` produce and check a compact HS256 JWT. `Claims` carry a subject, abilities, and an expiry;
  verify is constant-time and honours `exp`. A backend that shares the secret verifies with no
  database round-trip. `tok::requires_ability("room:library", secret)` is route middleware that gates
  on a bearer token's abilities.
- **Personal access tokens** (`personal_access_token.hpp`): `pat::Store` over the ORM `Connection` —
  `issue()` returns the plaintext once and stores only its SHA-256 hash; `authenticate()` matches the
  hash and checks expiry; `revoke()` by id. For long-lived API credentials where you own the database.

Abilities are plain strings with no built-in vocabulary — you define `room:*`, `post`, `api:read`, …
and the same language works across signed tokens, PATs, and gates.

## Queue & scheduler

`DbQueue` (`db_queue.hpp`) persists named jobs through the ORM `Connection` — the "database" queue
driver. Jobs support delays (`push(job, payload, delay)`), failed attempts back off and land in
`failed_jobs` at max attempts, and `work()` claims each due job with an atomic compare-and-set
(`Connection::update_if`), so several workers — threads or separate processes on one database —
never double-process; a job orphaned by a crashed worker is reclaimed after a timeout. The clock is
injectable, so none of it needs sleeps to test.

`Schedule` (`scheduler.hpp`) is the cron side: `every(seconds, name, fn)` and
`daily_at("HH:MM", name, fn)` (local time — a tick that comes late still runs today's task, once),
driven by `run_pending()` from real cron, a loop thread, or `cpp-artisan schedule:run`. Last-run
stamps persist through the `Connection`, so restarts don't double-fire. The intended pairing:
scheduled fns just `push()` jobs; `cpp-artisan queue:work` does the lifting. Transactions are
available on the same contract — `transaction(conn, fn)` commits on return and rolls back on throw.

## Boundaries (contracts)

Each major seam has an interface (`Illuminate\Contracts`-style), with the concrete class
implementing it and dependents depending on the *contract*, so each boundary is testable and
mockable in isolation:

- `ContainerContract` — the interesting one. Laravel's container interface keys on class-name
  strings resolved by reflection; with no reflection, our **virtual surface is type-erased**
  (`bind_erased`/`make_erased`, keyed by `std::type_index`/`std::any`) and the ergonomic template
  API (`bind<T>`/`resolve<T>`/`bind_auto<...>`) is **non-virtual sugar** over it — available
  through a `ContainerContract&` too. `Container` implements only the five primitives.
- `RouterContract` — owns `Route`/`RouteInfo`; virtual `add`/`match`/`list`, with `get`/`post` as
  shared non-virtual sugar. `Kernel` depends on this, so it runs against a **fake router** in
  `tests/test_kernel_contract.cpp` (no real Router needed).
- `KernelContract` — the narrow `handle(Request) -> Response` a client sees.
- `Connection` (in `database.hpp`) was already a contract; `MemoryConnection` implements it and a
  SQL driver can later implement the same interface.

Per-boundary unit tests (`tests/test_{container,router,kernel}_contract.cpp`) exercise each seam
through its interface — including code that only ever sees the abstraction.

## Container features

Beyond bind/singleton/instance/resolve, the container has the Laravel niceties, mapped onto the
type-erased core (tests in `tests/test_container_advanced.cpp`):

- **Contextual bindings** — `c.when<ServiceA>().needs<Logger>().give<FileLogger>();` resolves a
  different implementation of the same dependency per consumer. Implemented with a resolution-context
  stack: `bind_auto` marks the type under construction, and `make_erased` consults a per-consumer
  override map.
- **Tagged bindings** — `c.tag<Logger, FileLogger, NullLogger>("loggers");` then
  `c.tagged<Logger>("loggers")` returns `std::vector<std::shared_ptr<Logger>>`.
- **Scoped lifetimes** — `scoped` / `scoped_auto` memoise like a singleton but are dropped by
  `forget_scoped()` (e.g. per request).
- **Diagnostics** — unresolved bindings throw `BindingResolutionException` with **demangled** type
  names and the **build chain**, e.g.
  `No binding registered for [Logger] while building [ServiceA]`.

## The key finding: type erasure can't autowire

Type erasure is the right core for the container, **but it cannot autowire**. C++ has no runtime
reflection, so `resolve<T>()` can construct `T` but cannot discover what `T` needs. Every binding
currently hand-wires its dependencies in a factory lambda (see `app/bootstrap.cpp`):

```cpp
container->bind<UserController>([](Container& c) {
    return std::make_shared<UserController>(c.resolve<UserService>());
});
```

That manual wiring is the cost. Three approaches, in increasing effort:

1. **Explicit factory lambdas** — verbose, transparent, zero magic.
2. **Variadic resolve** — `resolve<UserController, UserService>()`; the container resolves and
   forwards the listed deps. Halves the boilerplate; deps still listed by hand.
3. **Codegen via libclang** — parse constructors at build time, emit factory lambdas.
   The only path to true "type-hint the constructor and it just works" Laravel DX.

One more thing to weigh: an unbound `resolve<T>()` fails at *runtime* (like Laravel), and type
erasure specifically prevents catching it at compile time. A statically-wired DI design would give
a compile error instead — worth considering per subsystem.

## Autowiring: the two approaches compared

Both were built; here's how they shake out:

| Approach | Used? | What it removes | Cost |
|----------|-------|-----------------|------|
| variadic `bind_auto<T, Deps...>` | yes, by default | the factory lambda | zero deps; deps still listed by hand |
| libclang codegen (`tools/autowire`) | available, off by default | the hand-listed deps too | libclang build dep + a codegen step |

### Variadic resolve (the default)

`Container::bind_auto` / `singleton_auto` resolve the listed deps and forward them to the
constructor, so there is no factory lambda to write:

```cpp
container->singleton_auto<UserService>();            // no dependencies
container->bind_auto<UserController, UserService>();  // ctor takes a UserService
```

The demo (`app/bootstrap.cpp`) uses these exclusively, with the original verbose form kept inline
as a comment for contrast. It's pure standard C++, no dependencies, and it makes the binding
self-documenting. The only residue is that the deps are still listed by hand — but that list is
short, explicit, and a compile error catches a wrong type immediately.

### libclang codegen (optional)

`tools/autowire/autowire.cpp` parses a header with libclang, finds each class's injection
constructor (parameters typed `std::shared_ptr<T>`), and emits `bind_auto<T, Deps...>()` calls.
The constructor signature becomes the single source of truth — the last manual step of variadic
resolve is gone. It works end to end: the `autowire_demo` CMake target codegens from `app/user_controller.hpp`,
compiles the output, and asserts the wiring resolves at runtime (run `ctest`). Build it with
`libclang-dev` installed; the whole tool is gated so the core build never depends on it.

Generated from the demo header:

```cpp
// GENERATED by tools/autowire — do not edit.
#include "container.hpp"
#include "user_controller.hpp"

void register_autowired(Container& container) {
    container.bind_auto<UserController, UserService>();
}
```

Why it's off by default, for honest reasons:

- **The cost is real.** It adds libclang (a heavy, version- and platform-sensitive dependency) and
  a codegen step to the build — generated artifacts, dependency ordering, a tool to maintain.
- **The gain is marginal.** All it removes over variadic resolve is typing `, UserService`. That
  list is already short and self-documenting, and listing it by hand gives a clearer compile error.
- **It can't close the loop anyway.** Lifetime (singleton vs transient) and interface→impl
  bindings *cannot* be inferred from a constructor signature, so a hand-written registration file
  never disappears — codegen only fills in its dep lists. (The tool emits `bind_auto` only; the
  `autowire_demo` driver still declares `UserService`'s singleton lifetime by hand.)

It would start to pay off if the binding count grew large enough that maintaining dep lists became
a real burden, or if attribute annotations (e.g. `[[singleton]]`) let codegen encode lifetime too —
at which point it could generate the whole registration file.

## Service providers & facades

### Service providers

`ServiceProvider` (`include/service_provider.hpp`) has the two Laravel phases, and the Kernel
runs them in order (`Kernel::register_providers`):

1. `register_()` on **every** provider — bind services; must not resolve.
2. `boot()` on **every** provider — runs only after all registers; may resolve and use services.

The split is the whole point: because nothing resolves until every binding exists, **provider order
doesn't matter**. `tests/test_providers.cpp` proves it — a provider listed *first* reads, in its
`boot()`, a binding made by a provider listed *second*. The demo's `UserServiceProvider` registers
`UserService`/`UserController` (via the autowiring helpers) and warms the singleton in `boot()`.

One wrinkle worth noting: `register` is a reserved keyword in C++, so the method is `register_()` —
the kind of small can't-map-cleanly detail this project tries to be honest about.

Service providers port cleanly: no magic and no hidden cost, just virtual methods and an ordered loop.

### Facades — provided, but injection is the default

`Facade` (`include/facade.hpp`) is a static proxy over a **process-global** container pointer. A
concrete facade derives from `FacadeFor<Service>` and forwards static calls:

```cpp
class Users : public FacadeFor<UserService> {
public:
    static std::string find(const std::string& id) { return root()->find(id); }
};

// call site, anywhere:  Users::find("42")
```

It works and reads nicely. But the static-state cost is sharper in C++ than in PHP:

- **Hidden dependency, runtime failure.** A function calling `Users::find()` looks dependency-free;
  it actually depends on `set_container()` having run. Forget it and you get a *runtime* throw, not
  a compile error (`tests/test_facades.cpp` pins this). Constructor injection makes the same
  dependency a compile-time signature.
- **Test isolation.** The global is shared across all call sites and tests, so tests must
  `clear_container()` to stay independent — the framework even has to *provide* that reset. Injected
  services are just locals; nothing to reset.
- **Concurrency.** PHP facades live inside a single-threaded per-request process. A C++ server is
  multithreaded, so the global container is shared mutable state — fine when set once at boot and
  only read after, but a footgun if anything rebinds at runtime.

What facades buy is terseness at the call site and Laravel-familiar DX. What injection buys is
compile-checked dependencies, trivial test isolation, and no global state — and since `bind_auto`
already makes injection nearly boilerplate-free, the usual "but wiring is tedious" argument for
facades is weak here.

So facades are provided as opt-in, but plain injection is the recommended default. Reach for a
facade only for app-level readability glue (set once at bootstrap, read-only thereafter); keep core
and library code on constructor injection. The demo wires `Facade::set_container` in `bootstrap()`
and uses `Users::find` once in `main.cpp` to exercise the path — deliberately sparingly.

## Eloquent (repository-style ORM)

Eloquent is the hardest part to port (it leans on magic attribute access and runtime reflection).
It takes the **repository** path, not ActiveRecord:

- `database.hpp` — a `Connection` contract plus `MemoryConnection`, a zero-dependency in-memory
  backend (a SQL driver can implement the same contract later without touching anything above).
  `Value`/`Row` are the dynamically-typed wire format, confined behind the mapper.
- `repository.hpp` — `Repository<T, Mapper>` with `find` / `all` / `where` / `insert` / `update` /
  `remove`. Models stay **fully typed**; nothing is stringly-typed.
- `app/models/article.hpp` — a plain `Article` struct and a hand-written `ArticleMapper`
  (table name, hydrate, to_row, id accessors). The mapper is the explicit stand-in for the
  reflection C++ lacks.
- `DatabaseServiceProvider` binds the `Connection` contract to `MemoryConnection` (an
  interface→implementation binding — exactly the kind the codegen tool can't infer) and registers
  the repository via `bind_auto`.

```cpp
auto articles = container.resolve<ArticleRepository>();
Article a{0, "Hello", 0, true};
articles->insert(a);                 // a.id assigned
auto got = articles->where("published", Value{true});
```

### Query builder & a second model

`repository.hpp` adds a backend-neutral `QueryBuilder<T, Mapper>` (cf. Eloquent's query builder):

```cpp
repo.query()
    .where("published", Value{true})
    .where("views", Op::Gte, Value{std::int64_t{10}})  // operators: Eq/Ne/Lt/Lte/Gt/Gte
    .order_by("views", /*descending=*/true)
    .limit(10)
    .get();                 // also: first(), count()
```

The builder accumulates a `Query` (wheres/order/limit) that the `Connection` executes — so it's
driver-agnostic: `MemoryConnection` filters/sorts/limits in memory today, and a SQL backend would
translate the same `Query` to SQL with nothing above it changing. A second model (`Author`) proves
the generics: it needed only its struct + mapper, no new repository/query code.

**Mapper codegen.** Each hand-written mapper is ~24 lines of mechanical code with every column name
repeated three times (struct field, `hydrate`, `to_row`). `tools/codegen/genmodel` (libclang)
generates exactly that from a plain model struct: it reads the fields and emits the `Mapper` +
`Repository<T, Mapper>` alias, byte-compatible with the hand-written ones. The build proves it end
to end — `codegen_demo` generates a mapper for a mapper-less `Widget` struct, compiles it, and
round-trips it through a `MemoryConnection` (run `ctest`). The boilerplate is fully generatable, so
once a project has many models, drop the hand-written mappers and generate them; at two or three
models, hand-writing is still fine. Build is gated on `libclang-dev`.

### Relationships & explicit soft deletes (a pattern, not a trait)

Relationships are explicit (`include/relations.hpp`), no `$model->comments` magic — each is a free
function returning a scoped `QueryBuilder` you can chain:

```cpp
has_many<Comment, CommentMapper>(conn, "article_id", article.id)
    .where("approved", Value{true}).get();
belongs_to<Article, ArticleMapper>(conn, comment.article_id);   // -> std::optional<Article>
has_one<Comment, CommentMapper>(conn, "article_id", article.id);
```

Soft deletes are kept explicit too: a model carries a `deleted` flag, "deleting" sets it
(`update`), and queries exclude it with `.where("deleted", Value{false})`. Laravel auto-scopes this
globally via a trait; we don't hide it — consistent with the project's no-magic stance. Tests in
`tests/test_relations.cpp`.

**Still out of scope:** model events.

### SQLite backend & migrations

`SqliteConnection` (`include/sqlite_connection.hpp` + `src/sqlite_connection.cpp`) implements the
same `Connection` contract over libsqlite3 — it translates the backend-neutral `Query` from
`QueryBuilder` into SQL, so `Repository`, relationships, and the query builder all work unchanged:

```cpp
auto db = std::make_shared<SqliteConnection>(":memory:");   // or a file path
Migrator(*db).run({ std::make_shared<CreateArticles>() });   // create the schema
ArticleRepository repo(db);                                  // same repository, real SQL
```

- **Migrations** (`schema.hpp` + `migration.hpp`): a `Blueprint`/`Schema::create` schema builder and
  a `Migrator` that runs pending migrations idempotently (tracked in a `migrations` table) with
  `rollback()`. Backend-agnostic — DDL is a no-op on the in-memory backend, real on SQLite.
- **Type coercion**: SQLite has no real boolean, so `Row::get<bool>` coerces from the returned
  INTEGER (`value_as`); the *same* mappers work across both backends with no changes.
- **Zero-dep core preserved**: `SqliteConnection` compiles only when libsqlite3 is found
  (CMake-gated) and links sqlite3 itself; the framework lib stays dependency-free. Tests:
  `tests/test_sqlite.cpp` (separate `test_sqlite` ctest target) — CRUD, query builder, migration
  idempotency, and rollback over a real database.

**The demo app is wired to SQLite.** When built with libsqlite3, `DatabaseServiceProvider` binds the
`Connection` to `SqliteConnection` and runs `app_migrations()` in `boot()`. The backend path comes
from `$LIPP_DB`, defaulting to an in-memory database — so each app instance (and each feature test)
gets a fresh DB, while a file makes it persist across processes:

```sh
LIPP_DB=demo.sqlite ./build/examples/blog/cpp-artisan request POST /articles "title=Hi&published=1" --token secret-token
LIPP_DB=demo.sqlite ./build/examples/blog/cpp-artisan request GET /articles     # -> [{"id":1,...}]  (survives restarts)
LIPP_DB=demo.sqlite ./build/examples/blog/cpp-artisan serve 8080
```

Without libsqlite3 the demo falls back to `MemoryConnection` — same behaviour, no persistence.
