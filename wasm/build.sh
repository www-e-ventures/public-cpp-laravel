#!/usr/bin/env bash
# Build the WebAssembly demo. Requires Emscripten (emcc) on PATH:
#   source /path/to/emsdk/emsdk_env.sh
#
# Compiles the framework (minus the socket HTTP server) + wasm/main.cpp to
# wasm/dist/lipp.{js,wasm}. Serve the wasm/ directory over HTTP and open index.html.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

command -v emcc >/dev/null || { echo "emcc not found — source emsdk_env.sh first" >&2; exit 1; }

mkdir -p wasm/dist
SRC="wasm/main.cpp src/pipeline.cpp src/router.cpp src/kernel.cpp src/database.cpp"
INC="-Iinclude -Iexamples/blog/app"

# 1) Browser module (JS + wasm) for index.html.
emcc -std=c++20 -O2 $INC $SRC \
  -o wasm/dist/lipp.js \
  -sMODULARIZE=1 -sEXPORT_NAME=createLipp \
  -sEXPORTED_FUNCTIONS=_lipp_handle,_lipp_status,_malloc,_free \
  -sEXPORTED_RUNTIME_METHODS=ccall,cwrap \
  -sENVIRONMENT=web,node \
  -sALLOW_MEMORY_GROWTH=1

# 2) Standalone WASI reactor (imports only wasi_snapshot_preview1) for a non-JS host,
#    e.g. the Wasmtime embedder in host.cpp. Uses the new (non-legacy) exceptions
#    proposal so Wasmtime's wasm_exceptions(true) accepts it.
emcc -std=c++20 -O2 -fwasm-exceptions -sWASM_LEGACY_EXCEPTIONS=0 $INC $SRC \
  -o wasm/dist/lipp_reactor.wasm \
  -sSTANDALONE_WASM=1 --no-entry \
  -sEXPORTED_FUNCTIONS=_lipp_handle,_lipp_status,_malloc,_free

echo "built wasm/dist/lipp.{js,wasm} and wasm/dist/lipp_reactor.wasm"
echo "browser:  (cd wasm && python3 -m http.server 8000)  then open http://localhost:8000/"
echo "C++ host: WASMTIME_DIR=... ./wasm/host-build.sh && ./wasm/dist/lipp_host"
