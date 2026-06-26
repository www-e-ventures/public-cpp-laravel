// test_harness.hpp — tiny zero-dependency test harness.
//
// No gtest, no third-party deps. Tests self-register via the
// TEST(name) macro; assertions use CHECK / CHECK_EQ. Run them with RUN_ALL_TESTS()
// from main(), which prints per-test results and an "N passing" summary and returns
// a process exit code (0 = all green).
#pragma once
#include <functional>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace test {

struct Case {
    std::string name;
    std::function<void(int&)> fn; // arg: failure counter for this case
};

inline std::vector<Case>& registry() {
    static std::vector<Case> r;
    return r;
}

struct Registrar {
    Registrar(std::string name, std::function<void(int&)> fn) {
        registry().push_back({std::move(name), std::move(fn)});
    }
};

template <typename A, typename B>
void report_eq_failure(const std::string& expr_a, const std::string& expr_b,
                       const A& a, const B& b, const char* file, int line, int& fails) {
    std::ostringstream os;
    os << "    FAIL " << file << ":" << line << "  " << expr_a << " == " << expr_b
       << "\n         got: " << a << "\n         vs:  " << b;
    std::cerr << os.str() << "\n";
    ++fails;
}

inline void report_failure(const std::string& expr, const char* file, int line, int& fails) {
    std::cerr << "    FAIL " << file << ":" << line << "  CHECK(" << expr << ")\n";
    ++fails;
}

inline int run_all() {
    int total_fails = 0;
    int passed_cases = 0;
    for (auto& c : registry()) {
        int fails = 0;
        c.fn(fails);
        if (fails == 0) {
            std::cout << "  ok   " << c.name << "\n";
            ++passed_cases;
        } else {
            std::cout << "  FAIL " << c.name << " (" << fails << " assertion(s))\n";
        }
        total_fails += fails;
    }
    const auto total = registry().size();
    std::cout << "\n" << passed_cases << "/" << total << " passing";
    if (total_fails > 0) std::cout << "  (" << total_fails << " failed assertion(s))";
    std::cout << "\n";
    return total_fails == 0 ? 0 : 1;
}

} // namespace test

// __fails is injected into each test body by the TEST macro.
#define TEST(name)                                                     \
    static void name(int& __fails);                                    \
    static const ::test::Registrar __reg_##name(#name, name);          \
    static void name([[maybe_unused]] int& __fails)

#define CHECK(cond)                                                    \
    do {                                                               \
        if (!(cond)) ::test::report_failure(#cond, __FILE__, __LINE__, __fails); \
    } while (0)

#define CHECK_EQ(a, b)                                                 \
    do {                                                               \
        auto&& __a = (a);                                              \
        auto&& __b = (b);                                              \
        if (!(__a == __b))                                             \
            ::test::report_eq_failure(#a, #b, __a, __b, __FILE__, __LINE__, __fails); \
    } while (0)

#define RUN_ALL_TESTS() ::test::run_all()
