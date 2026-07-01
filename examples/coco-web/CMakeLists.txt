# examples/coco-web — a minimal reference app: a static SPA + a JSON API on
# cpp-laravel. Demonstrates static file serving (v0.3.0) and token-gated routes
# (v0.4.0) end to end. Needs OpenSSL (for the token middleware); skipped otherwise.
cmake_minimum_required(VERSION 3.21)
project(coco_web LANGUAGES CXX)

find_package(OpenSSL QUIET)
if(NOT OpenSSL_FOUND)
    message(STATUS "coco-web example: OpenSSL not found — skipping")
    return()
endif()

# COCO_WEB_PUBLIC_DIR points the app at this example's public/ dir so the demo and
# the test both find index.html / assets regardless of the working directory.
add_executable(coco_web main.cpp)
target_link_libraries(coco_web PRIVATE framework OpenSSL::Crypto)
target_include_directories(coco_web PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(coco_web
    PRIVATE COCO_WEB_PUBLIC_DIR="${CMAKE_CURRENT_SOURCE_DIR}/public")

if(PROJECT_IS_TOP_LEVEL OR CMAKE_PROJECT_NAME STREQUAL "lipp")
    add_executable(coco_web_tests tests/test_coco_web.cpp)
    target_link_libraries(coco_web_tests PRIVATE framework OpenSSL::Crypto)
    target_include_directories(coco_web_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_compile_definitions(coco_web_tests
        PRIVATE COCO_WEB_PUBLIC_DIR="${CMAKE_CURRENT_SOURCE_DIR}/public")
    add_test(NAME coco_web COMMAND coco_web_tests)
endif()
