#!/usr/bin/env bash
# Build the C++ host (host.cpp) that embeds the Wasmtime runtime and runs the framework
# compiled to WebAssembly (wasm/dist/lipp_reactor.wasm) — no JavaScript involved.
#
# Needs the Wasmtime C API. Download a *-linux-c-api.tar.xz from
#   https://github.com/bytecodealliance/wasmtime/releases
# extract it, and point WASMTIME_DIR at the directory containing include/ and lib/.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
: "${WASMTIME_DIR:?set WASMTIME_DIR to the extracted wasmtime c-api dir (has include/ + lib/)}"

[ -f wasm/dist/lipp_reactor.wasm ] || { echo "build the reactor first: ./wasm/build.sh" >&2; exit 1; }

g++ -std=c++20 -O2 wasm/host.cpp \
  -I"$WASMTIME_DIR/include" \
  -l:libwasmtime.a -L"$WASMTIME_DIR/lib" -lpthread -ldl -lm \
  -o wasm/dist/lipp_host

echo "built wasm/dist/lipp_host — run:  ./wasm/dist/lipp_host"
