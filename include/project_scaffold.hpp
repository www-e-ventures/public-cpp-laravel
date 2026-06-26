// project_scaffold.hpp — `cpp-artisan new`: scaffold a standalone app on the framework.
//
// Emits a minimal but working project (a Post CRUD resource) whose CMake pulls the
// framework in via FetchContent — or a local checkout when -DLIPP_SOURCE_DIR=... is
// passed. This is the "laravel new" of the project: users code over the framework
// without its demo/tests in the way.
#pragma once
#include <string>

#include "generators.hpp" // render_model + write_new_file

namespace scaffold {

inline std::string sub(std::string s, const std::string& token, const std::string& value) {
    for (std::size_t p = s.find(token); p != std::string::npos; p = s.find(token, p + value.size()))
        s.replace(p, token.size(), value);
    return s;
}

inline std::string cmake_txt(const std::string& project) {
    return sub(R"(cmake_minimum_required(VERSION 3.21)
project(@ LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Pull in the framework. Override with -DLIPP_SOURCE_DIR=/path/to/cpp-laravel to use
# a local checkout instead of fetching from git.
include(FetchContent)
if(DEFINED LIPP_SOURCE_DIR)
    add_subdirectory(${LIPP_SOURCE_DIR} _lipp)
else()
    FetchContent_Declare(lipp
        GIT_REPOSITORY https://github.com/www-e-ventures/cpp-laravel.git
        GIT_TAG main)
    FetchContent_MakeAvailable(lipp)
endif()

add_library(app STATIC app/bootstrap.cpp)
target_include_directories(app PUBLIC app)
target_link_libraries(app PUBLIC framework)

add_executable(@ main.cpp)
target_link_libraries(@ PRIVATE app)

enable_testing()
add_executable(@_tests tests/run_main.cpp tests/feature_test.cpp)
target_link_libraries(@_tests PRIVATE app)
add_test(NAME app COMMAND @_tests)
)",
               "@", project);
}

inline std::string post_controller_hpp() {
    return R"(// post_controller.hpp — a CRUD controller over PostRepository.
#pragma once
#include <memory>
#include <string>
#include <utility>

#include "http.hpp"
#include "post.hpp"

class PostController {
public:
    explicit PostController(std::shared_ptr<PostRepository> repo) : repo_(std::move(repo)) {}

    Response index(Request&) {
        return Response::json("{\"count\":" + std::to_string(repo_->all().size()) + "}");
    }
    Response store(Request& req) {
        Post p;
        p.name = req.body; // body is the post name (keep the starter simple)
        repo_->insert(p);
        return Response::json("{\"id\":" + std::to_string(p.id) + "}", 201);
    }

private:
    std::shared_ptr<PostRepository> repo_;
};
)";
}

inline std::string bootstrap_hpp() {
    return R"(// bootstrap.hpp — wires the app (container, routes, kernel).
#pragma once
#include <memory>

#include "container.hpp"
#include "kernel.hpp"
#include "router.hpp"

struct App {
    std::shared_ptr<Container> container;
    std::shared_ptr<Router> router;
    std::shared_ptr<Kernel> kernel;
};

App bootstrap();
)";
}

inline std::string bootstrap_cpp() {
    return R"(// bootstrap.cpp — see bootstrap.hpp
#include "bootstrap.hpp"

#include "database.hpp"
#include "post.hpp"
#include "post_controller.hpp"

App bootstrap() {
    auto c = std::make_shared<Container>();

    // In-memory backend by default (swap for SqliteConnection when you want SQL).
    c->singleton<Connection>([](ContainerContract&) {
        return std::static_pointer_cast<Connection>(std::make_shared<MemoryConnection>());
    });
    c->bind_auto<PostRepository, Connection>();
    c->bind_auto<PostController, PostRepository>();

    auto r = std::make_shared<Router>();
    r->get("/posts", [c](Request& rq) { return c->resolve<PostController>()->index(rq); });
    r->post("/posts", [c](Request& rq) { return c->resolve<PostController>()->store(rq); });

    auto k = std::make_shared<Kernel>(c, r);
    return App{c, r, k};
}
)";
}

inline std::string main_cpp(const std::string& project) {
    return sub(R"(// main.cpp — @ entry point.
#include <iostream>

#include "bootstrap.hpp"

int main() {
    App app = bootstrap();
    app.kernel->handle({"POST", "/posts", {}, {}, "Hello"});
    Response r = app.kernel->handle({"GET", "/posts", {}, {}, ""});
    std::cout << "@: GET /posts -> " << r.body << "\n";
    return 0;
}
)",
               "@", project);
}

inline std::string feature_test_cpp() {
    return R"(// feature_test.cpp — a Laravel-flavoured feature test.
#include "test_harness.hpp"

#include "bootstrap.hpp"
#include "testing.hpp"

TEST(creates_and_counts_posts) {
    auto app = bootstrap();
    HttpClient http(*app.kernel, __fails);

    http.post("/posts", "Hello").assertCreated().assertSee("\"id\":1");
    http.get("/posts").assertOk().assertSee("\"count\":1");
}
)";
}

inline std::string run_main_cpp() {
    return R"(#include "test_harness.hpp"

int main() { return RUN_ALL_TESTS(); }
)";
}

inline std::string readme_md(const std::string& project) {
    return sub(R"(# @

A C++ web app built on the [Laravel-in-C++ framework](https://github.com/www-e-ventures/cpp-laravel).

## Build & run

```sh
cmake -S . -B build              # fetches the framework via FetchContent
cmake --build build
./build/@                        # runs the demo
ctest --test-dir build           # runs the feature tests
```

To build against a local checkout of the framework instead of fetching:

```sh
cmake -S . -B build -DLIPP_SOURCE_DIR=/path/to/cpp-laravel
```

## Layout

- `app/` — your code: `post.hpp` (model + mapper + repository), `post_controller.hpp`, `bootstrap.cpp`.
- `main.cpp` — entry point.
- `tests/` — feature tests using the framework's Laravel-flavoured HTTP DSL.

Add a model + mapper with the framework's `cpp-artisan make:model <Name>`.
)",
               "@", project);
}

inline std::string gitignore() { return "/build/\nbuild*/\n*.o\n*.a\n"; }

// Write the whole project tree. Returns false (and sets err) on the first failure.
inline bool create_project(const std::string& project, const std::string& dir, std::string& err) {
    return write_new_file(dir + "/CMakeLists.txt", cmake_txt(project), err) &&
           write_new_file(dir + "/.gitignore", gitignore(), err) &&
           write_new_file(dir + "/README.md", readme_md(project), err) &&
           write_new_file(dir + "/main.cpp", main_cpp(project), err) &&
           write_new_file(dir + "/app/post.hpp", render_model("Post"), err) &&
           write_new_file(dir + "/app/post_controller.hpp", post_controller_hpp(), err) &&
           write_new_file(dir + "/app/bootstrap.hpp", bootstrap_hpp(), err) &&
           write_new_file(dir + "/app/bootstrap.cpp", bootstrap_cpp(), err) &&
           write_new_file(dir + "/tests/run_main.cpp", run_main_cpp(), err) &&
           write_new_file(dir + "/tests/feature_test.cpp", feature_test_cpp(), err);
}

} // namespace scaffold
