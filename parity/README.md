# Parity oracle

Proves the C++ app is **behaviourally faithful** to a real PHP stack: one phpunit suite
(`tests/ArticlesParityTest.php`, written in Laravel/`Illuminate\Testing` style) runs against two
backends and must pass identically:

1. the C++ app via `cpp-artisan serve`, and
2. a small vanilla-PHP reference app (`php-app/index.php`) with the same Articles routes.

The suite is backend-agnostic — it targets `BASE_URL` over HTTP — so it's the oracle, and either app
is a candidate implementation.

## Run

```sh
cd parity
composer install        # one-time: installs phpunit (needs network)
cd ..
./parity/run-parity.sh  # boots each backend, runs the suite against both
```

Expected tail:

```
PARITY OK — the same phpunit suite passes against both the C++ app and the PHP reference.
```

## Notes

- The PHP reference app uses SQLite (`pdo_sqlite`) so state persists across requests under
  `php -S`; tests are self-contained (they capture the created id) so no DB reset is needed.
- This is an **optional** track — it's the only part of the project that needs a PHP/Composer
  toolchain. The C++ unit + feature tests (`ctest`) remain the primary suite; this exists to
  pin behavioural fidelity to Laravel conventions.
- `vendor/` is gitignored; run `composer install` to recreate it.
