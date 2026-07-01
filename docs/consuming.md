# Building an app on cpp-laravel

cpp-laravel ships a CMake `framework` target (plus an optional `framework_sqlite`).
There are three supported ways to depend on it. Pin a tag and upgrade deliberately;
see [`CHANGELOG.md`](../CHANGELOG.md) for what changed between versions.

## 1. FetchContent (recommended for most consumers)

Pins a tagged version and builds it as part of your project — no install step.

```cmake
include(FetchContent)
FetchContent_Declare(cpplaravel
  GIT_REPOSITORY https://github.com/www-e-ventures/public-cpp-laravel.git
  GIT_TAG        v0.2.0)
FetchContent_MakeAvailable(cpplaravel)

add_executable(yourapp main.cpp)
target_link_libraries(yourapp PRIVATE framework)     # + framework_sqlite for SQLite
```

## 2. Vendored `add_subdirectory` (offline / air-gapped builds)

Copy `include/`, `src/`, `cmake/`, and `CMakeLists.txt` into a `framework/` directory
in your repo and build it as a subdirectory. Useful where fetching at build time isn't
reliable (offline boxes, reproducible builds). Record the tag you vendored so you can
re-vendor deliberately on upgrade.

```cmake
add_subdirectory(framework)
target_link_libraries(yourapp PRIVATE framework)
```

## 3. Installed package (`find_package`)

Install once, then find it from any project:

```sh
cmake -S . -B build && cmake --build build
cmake --install build --prefix /opt/lipp
```

```cmake
find_package(lipp CONFIG REQUIRED)
target_link_libraries(yourapp PRIVATE lipp::framework)
```

## Notes

- **SQLite is optional.** Link `framework_sqlite` only if you want the SQLite-backed
  `Connection`; otherwise the in-memory `MemoryConnection` needs nothing extra.
- **The core is dependency-free.** OpenSSL-backed pieces (PBKDF2 password hashing,
  WebSocket accept keys) live in a separate optional layer — you opt in by linking them.
- **C++20** and `-Wall -Wextra -Werror` clean. Your app supplies its own `main()` and
  wires the container/providers/router; see the `examples/` directory for a full app.
