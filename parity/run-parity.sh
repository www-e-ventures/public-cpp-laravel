#!/usr/bin/env bash
# Behavioural parity oracle: run the SAME phpunit suite against (1) the C++ server
# and (2) the PHP reference app. Both must pass — that's the parity guarantee.
set -euo pipefail

PARITY="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$PARITY/.." && pwd)"
PHPUNIT="$PARITY/vendor/bin/phpunit"
CPP_PORT=8091
PHP_PORT=8092

if [[ ! -x "$PHPUNIT" ]]; then
    echo "phpunit not installed — run: (cd parity && composer install)" >&2
    exit 1
fi

# Ensure the C++ server binary exists.
cmake -S "$ROOT" -B "$ROOT/build" >/dev/null
cmake --build "$ROOT/build" --target cpp-artisan >/dev/null
ARTISAN="$ROOT/build/examples/blog/cpp-artisan"

wait_up() { for _ in $(seq 1 40); do curl -s -o /dev/null "$1/health" && return 0; sleep 0.1; done; return 1; }
run_suite() { BASE_URL="$1" "$PHPUNIT" -c "$PARITY/phpunit.xml"; }

# (1) C++ backend
"$ARTISAN" serve "$CPP_PORT" >/tmp/cpp_parity.log 2>&1 &
CPP_PID=$!
trap 'kill $CPP_PID 2>/dev/null || true' EXIT
wait_up "http://127.0.0.1:$CPP_PORT"
echo "### Suite vs C++ server (port $CPP_PORT)"
run_suite "http://127.0.0.1:$CPP_PORT"
kill $CPP_PID 2>/dev/null || true; trap - EXIT

# (2) PHP reference backend
rm -f /tmp/parity_articles.sqlite
PARITY_DB=/tmp/parity_articles.sqlite php -S "127.0.0.1:$PHP_PORT" "$PARITY/php-app/index.php" \
    >/tmp/php_parity.log 2>&1 &
PHP_PID=$!
trap 'kill $PHP_PID 2>/dev/null || true' EXIT
wait_up "http://127.0.0.1:$PHP_PORT"
echo "### Suite vs PHP reference app (port $PHP_PORT)"
run_suite "http://127.0.0.1:$PHP_PORT"
kill $PHP_PID 2>/dev/null || true; trap - EXIT

echo
echo "PARITY OK — the same phpunit suite passes against both the C++ app and the PHP reference."
