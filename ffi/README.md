# FFI host (PHP)

The request lifecycle as a native shared library, called from PHP via FFI — **no JavaScript and
no WebAssembly**. `lipp_handle` is a plain C function (`extern "C"`), so any C-FFI host can drive
the framework; PHP is the example here, but Python `ctypes`, Rust, Go cgo, etc. work the same way.

`ffi/dist/liblipp.so` is built from the *same* `wasm/main.cpp` entry points compiled natively
(no Emscripten). It's the in-memory Articles app: router → middleware → container → controller →
repository ORM → Blade.

## Run

Needs PHP with FFI (PHP 7.4+). On the CLI, FFI::cdef needs `ffi.enable=1`:

```sh
./ffi/build.sh
php -d ffi.enable=1 ffi/demo.php
```

Expected:

```
GET   /articles        -> HTTP 200  [{"id":1,...},{"id":2,...}]
POST  /articles        -> HTTP 201  {"id":3,"title":"Created from PHP",...}
GET   /articles.html   -> HTTP 200  <ul><li>...</li></ul>      (Blade-rendered)
POST  /articles        -> HTTP 422  {"error":"title is required"}
GET   /nope            -> HTTP 404  Not Found
```

The C++ in-memory ORM keeps state across calls within the process (the `POST` shows up in the next
`GET`). `ffi/dist/` is generated (gitignored).
