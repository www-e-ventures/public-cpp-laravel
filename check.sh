#!/usr/bin/env bash
# check.sh — the release gate: build BOTH configs under -Wall -Wextra -Werror and
# run the suite in each. The default (no CMAKE_BUILD_TYPE) build alone is not
# enough — optimized builds see more (e.g. -Wdangling-pointer only fires at -O2),
# so a tag must be green here, not just in the dev build.
set -euo pipefail
cd "$(dirname "$0")"

for cfg in "" "Release"; do
    build="build-check${cfg:+-$(echo "$cfg" | tr '[:upper:]' '[:lower:]')}"
    label="${cfg:-default}"
    echo "== configure/build/test: $label ($build) =="
    cmake -S . -B "$build" ${cfg:+-DCMAKE_BUILD_TYPE=$cfg} >/dev/null
    cmake --build "$build" -j"$(nproc)"
    ctest --test-dir "$build" --output-on-failure
done

echo "== all green (default + Release) =="
