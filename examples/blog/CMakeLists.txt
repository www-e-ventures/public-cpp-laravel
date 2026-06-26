# examples/blog — a demo application built ON the framework.
# When built standalone (FetchContent / add_subdirectory of the framework), this is
# the shape a user's project takes. Here it builds as part of the framework repo.
cmake_minimum_required(VERSION 3.21)
project(blog LANGUAGES CXX)

# ── App library: models, controllers, providers, bootstrap ──
add_library(blog_app STATIC
    app/middleware.cpp
    app/bootstrap.cpp
    app/providers.cpp
)
target_include_directories(blog_app PUBLIC app)
target_link_libraries(blog_app PUBLIC framework)

# Use the real SQLite backend when available (framework_sqlite carries the
# LIPP_HAVE_SQLITE define + sqlite link); otherwise the in-memory backend.
if(TARGET framework_sqlite)
    target_link_libraries(blog_app PUBLIC framework_sqlite)
endif()

# ── Demo executable + console CLI ──
add_executable(demo main.cpp)
target_link_libraries(demo PRIVATE blog_app)

add_executable(cpp-artisan artisan.cpp)
target_link_libraries(cpp-artisan PRIVATE blog_app)

# ── App test suite (also exercises framework integration) ──
if(PROJECT_IS_TOP_LEVEL OR CMAKE_PROJECT_NAME STREQUAL "lipp")
    add_executable(blog_tests
        tests/run_main.cpp
        tests/test_main.cpp
        tests/test_providers.cpp
        tests/test_facades.cpp
        tests/test_orm.cpp
        tests/test_query_builder.cpp
        tests/test_relations.cpp
        tests/test_feature_blog.cpp
        tests/test_feature_livewire.cpp
        tests/test_feature_auth.cpp
        tests/test_feature_throttle.cpp
    )
    target_link_libraries(blog_tests PRIVATE blog_app)
    add_test(NAME blog COMMAND blog_tests)

    if(TARGET framework_sqlite)
        add_executable(blog_sqlite tests/test_sqlite.cpp)
        target_link_libraries(blog_sqlite PRIVATE blog_app)
        add_test(NAME blog_sqlite COMMAND blog_sqlite)
    endif()
endif()
