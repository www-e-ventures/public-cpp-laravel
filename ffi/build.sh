#!/usr/bin/env bash
# Build the request lifecycle as a native C-ABI shared library (liblipp.so).
# Any language with C FFI — PHP, Python, Rust, Go, ... — can then call lipp_handle.
# No JS, no WebAssembly: this is the same C++ as the demo, in a .so.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

mkdir -p ffi/dist
g++ -std=c++20 -O2 -fPIC -shared -Iinclude -Iexamples/blog/app \
  wasm/main.cpp src/pipeline.cpp src/router.cpp src/kernel.cpp src/database.cpp \
  -o ffi/dist/liblipp.so

echo "built ffi/dist/liblipp.so"
echo "run:  php -d ffi.enable=1 ffi/demo.php"
