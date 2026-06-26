# WebAssembly demo

The framework compiled to WebAssembly: the request lifecycle (router → middleware → container →
controller → repository ORM → Blade) runs **in the browser**, with no server and no network. It's a
demo of the compiled C++ executing client-side, not a deployment model — the request never leaves
the page.

The socket HTTP server is left out (browsers have no sockets); everything above it is pure compute
and compiles unchanged. `wasm/main.cpp` wires a small Articles app and exposes two C entry points to
JavaScript:

- `lipp_handle(method, path, body)` → response body
- `lipp_status()` → the last response's HTTP status

## Build

Needs [Emscripten](https://emscripten.org). With `emsdk` installed:

```sh
source /path/to/emsdk/emsdk_env.sh
./wasm/build.sh                       # -> wasm/dist/lipp.{js,wasm}
(cd wasm && python3 -m http.server 8000)
# open http://localhost:8000/
```

`wasm/dist/` is generated (gitignored). The page lets you issue requests and run a benchmark; in a
quick node run, 100k `GET /articles/1` went through the compiled kernel in ~0.27 s
(~360k req/s, including the JS↔WASM call overhead).

## C++ host (Wasmtime) — no JS at all

`host.cpp` embeds the [Wasmtime](https://wasmtime.dev) runtime and runs the framework-as-WASM from
C++: it loads the **standalone WASI reactor** (`lipp_reactor.wasm`, which imports only
`wasi_snapshot_preview1`), writes each request's strings into the module's linear memory via its
exported `malloc`, calls `lipp_handle`, and reads the response back out. C++ running
C++-compiled-to-WASM, zero JavaScript.

```sh
# 1. download the Wasmtime C API (*-linux-c-api.tar.xz) from the releases page, extract it
export WASMTIME_DIR=/path/to/wasmtime-vXX-x86_64-linux-c-api
# 2. build the reactor + the host, then run
./wasm/build.sh
./wasm/host-build.sh
./wasm/dist/lipp_host
```

Output: `GET/POST/Blade/422/404` driven entirely from the C++ host. (The reactor is built with the
non-legacy wasm exceptions proposal so Wasmtime's `wasm_exceptions(true)` accepts it.)

## Run headless (node)

```sh
node --input-type=module -e '
  const createLipp = (await import("./wasm/dist/lipp.js")).default;
  const M = await createLipp();
  const call = (m,p,b="") => ({
    status: (M.ccall("lipp_handle","string",["string","string","string"],[m,p,b]),
             M.ccall("lipp_status","number",[],[])),
    body: M.ccall("lipp_handle","string",["string","string","string"],[m,p,b]),
  });
  console.log(call("GET","/articles"));
'
```
