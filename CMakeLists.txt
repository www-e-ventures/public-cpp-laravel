cmake_minimum_required(VERSION 3.21)
project(lipp VERSION 0.1.0 LANGUAGES CXX)

# C++20, no compiler extensions.
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ── Framework: the reusable runtime, built as a static lib (zero-dependency core).
# This is the ONLY target a consumer needs; everything below is guarded so that
# pulling the framework in via FetchContent/add_subdirectory builds just this.
add_library(framework STATIC
    src/pipeline.cpp
    src/router.cpp
    src/kernel.cpp
    src/database.cpp
    src/http_server.cpp
)
target_include_directories(framework PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>)
target_compile_features(framework PUBLIC cxx_std_20)

# The HTTP server uses std::thread; the container/connections are mutex-guarded.
find_package(Threads REQUIRED)
target_link_libraries(framework PUBLIC Threads::Threads)

# ── Optional: real SQLite backend, as a separate lib so the core stays zero-dep.
# Exposed to consumers too (the example links it when present).
find_path(SQLITE3_INCLUDE_DIR sqlite3.h)
find_library(SQLITE3_LIB sqlite3)
if(SQLITE3_INCLUDE_DIR AND SQLITE3_LIB)
    message(STATUS "libsqlite3 found — building framework_sqlite (${SQLITE3_LIB})")
    add_library(framework_sqlite STATIC src/sqlite_connection.cpp)
    target_include_directories(framework_sqlite PRIVATE ${SQLITE3_INCLUDE_DIR})
    target_link_libraries(framework_sqlite PUBLIC framework ${SQLITE3_LIB})
    target_compile_definitions(framework_sqlite PUBLIC LIPP_HAVE_SQLITE)
else()
    message(STATUS "libsqlite3 not found — SQLite backend disabled (install libsqlite3-dev)")
endif()

# ── Install / package config: lets consumers `find_package(lipp)` (the non-FetchContent
# path). Only when building the framework directly. ──
if(PROJECT_IS_TOP_LEVEL)
    include(GNUInstallDirs)
    include(CMakePackageConfigHelpers)

    set(LIPP_EXPORT_TARGETS framework)
    if(TARGET framework_sqlite)
        list(APPEND LIPP_EXPORT_TARGETS framework_sqlite)
    endif()

    install(TARGETS ${LIPP_EXPORT_TARGETS} EXPORT lippTargets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
    install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
    install(EXPORT lippTargets NAMESPACE lipp:: FILE lippTargets.cmake
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/lipp)

    configure_package_config_file(cmake/lippConfig.cmake.in
        ${CMAKE_BINARY_DIR}/lippConfig.cmake
        INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/lipp)
    write_basic_package_version_file(${CMAKE_BINARY_DIR}/lippConfigVersion.cmake
        VERSION ${PROJECT_VERSION} COMPATIBILITY SameMajorVersion)
    install(FILES ${CMAKE_BINARY_DIR}/lippConfig.cmake ${CMAKE_BINARY_DIR}/lippConfigVersion.cmake
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/lipp)
endif()

# Everything beyond the libraries (tests, dev tools, example app) is only built when
# this project is the top-level build — consumers get just the libraries above.
if(PROJECT_IS_TOP_LEVEL)
    enable_testing()
    add_compile_options(-Wall -Wextra -Werror)

    # ── Framework unit tests (link only the framework) ──
    add_executable(framework_tests
        tests/run_main.cpp
        tests/test_autowire.cpp
        tests/test_http_server.cpp
        tests/test_static_files.cpp
        tests/test_container_contract.cpp
        tests/test_container_advanced.cpp
        tests/test_router_contract.cpp
        tests/test_kernel_contract.cpp
        tests/test_generators.cpp
        tests/test_config.cpp
        tests/test_validation.cpp
        tests/test_events.cpp
        tests/test_view.cpp
        tests/test_concurrency.cpp
        tests/test_livewire.cpp
        tests/test_services.cpp
        tests/test_session.cpp
        tests/test_auth.cpp
        tests/test_http_auth.cpp
        tests/test_scheduler.cpp
        tests/test_database.cpp
        tests/test_gate.cpp
        tests/test_hash.cpp
        tests/test_broadcast.cpp
    )
    target_link_libraries(framework_tests PRIVATE framework)
    add_test(NAME framework COMMAND framework_tests)

    # Optional: PBKDF2 password hasher (needs OpenSSL). Separate test exe so the core
    # framework stays dependency-free; the header drops in for anyone who links crypto.
    find_package(OpenSSL QUIET)
    if(OpenSSL_FOUND)
        message(STATUS "OpenSSL found — building PBKDF2 hasher test")
        add_executable(test_crypto tests/test_crypto.cpp)
        target_link_libraries(test_crypto PRIVATE framework OpenSSL::Crypto)
        add_test(NAME crypto COMMAND test_crypto)

        add_executable(test_websocket tests/test_websocket.cpp)
        target_link_libraries(test_websocket PRIVATE framework OpenSSL::Crypto)
        add_test(NAME websocket COMMAND test_websocket)

        add_executable(test_tokens tests/test_tokens.cpp)
        target_link_libraries(test_tokens PRIVATE framework OpenSSL::Crypto)
        add_test(NAME tokens COMMAND test_tokens)
    else()
        message(STATUS "OpenSSL not found — skipping PBKDF2 hasher (install libssl-dev)")
    endif()

    # ── Dev tools: libclang codegen (autowire + model mapper), gated on libclang ──
    find_path(CLANG_C_INCLUDE_DIR clang-c/Index.h
        PATHS /usr/lib/llvm-18/include /usr/lib/llvm-17/include /usr/include)
    find_library(CLANG_LIB NAMES clang clang-18 clang-17
        PATHS /usr/lib/llvm-18/lib /usr/lib/llvm-17/lib /usr/lib/x86_64-linux-gnu)

    if(CLANG_C_INCLUDE_DIR AND CLANG_LIB)
        message(STATUS "libclang found — building codegen tools (${CLANG_LIB})")

        add_executable(autowire tools/autowire/autowire.cpp)
        target_include_directories(autowire PRIVATE ${CLANG_C_INCLUDE_DIR})
        target_link_libraries(autowire PRIVATE ${CLANG_LIB})

        set(GEN_CPP ${CMAKE_BINARY_DIR}/autowired.gen.cpp)
        add_custom_command(
            OUTPUT ${GEN_CPP}
            COMMAND autowire ${CMAKE_SOURCE_DIR}/tools/autowire/sample.hpp -o ${GEN_CPP}
                    -- -std=c++20 -I${CMAKE_SOURCE_DIR}/include -I${CMAKE_SOURCE_DIR}/tools/autowire
            DEPENDS autowire ${CMAKE_SOURCE_DIR}/tools/autowire/sample.hpp
            COMMENT "autowire: codegen registrations from tools/autowire/sample.hpp"
            VERBATIM)

        add_executable(autowire_demo tools/autowire/verify_driver.cpp ${GEN_CPP})
        target_include_directories(autowire_demo PRIVATE ${CMAKE_SOURCE_DIR}/tools/autowire)
        target_link_libraries(autowire_demo PRIVATE framework)
        add_test(NAME autowire COMMAND autowire_demo)

        add_executable(cpp-codegen tools/codegen/genmodel.cpp)
        target_include_directories(cpp-codegen PRIVATE ${CLANG_C_INCLUDE_DIR})
        target_link_libraries(cpp-codegen PRIVATE ${CLANG_LIB})

        set(GEN_MODEL ${CMAKE_BINARY_DIR}/widget.gen.hpp)
        add_custom_command(
            OUTPUT ${GEN_MODEL}
            COMMAND cpp-codegen ${CMAKE_SOURCE_DIR}/tools/codegen/demo_model.hpp -o ${GEN_MODEL}
                    -- -std=c++20 -I${CMAKE_SOURCE_DIR}/include -I${CMAKE_SOURCE_DIR}/tools/codegen
            DEPENDS cpp-codegen ${CMAKE_SOURCE_DIR}/tools/codegen/demo_model.hpp
            COMMENT "genmodel: codegen mapper from tools/codegen/demo_model.hpp"
            VERBATIM)

        add_executable(codegen_demo tools/codegen/demo_driver.cpp ${GEN_MODEL})
        target_include_directories(codegen_demo PRIVATE ${CMAKE_BINARY_DIR} ${CMAKE_SOURCE_DIR}/tools/codegen)
        target_link_libraries(codegen_demo PRIVATE framework)
        add_test(NAME codegen COMMAND codegen_demo)
    else()
        message(STATUS "libclang not found — skipping codegen tools (install libclang-dev)")
    endif()

    # ── The example applications (each its own project under examples/) ──
    add_subdirectory(examples/blog)
    add_subdirectory(examples/coco-web)
endif()
