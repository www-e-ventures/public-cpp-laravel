// test_container_advanced.cpp — step 4 container hardening:
// contextual bindings, tagged bindings, scoped lifetimes, and diagnostics.
#include "test_harness.hpp"

#include <memory>
#include <string>
#include <utility>

#include "container.hpp"
#include "container_contract.hpp"

namespace {

struct Logger {
    virtual ~Logger() = default;
    virtual std::string name() const = 0;
};
struct FileLogger : Logger {
    std::string name() const override { return "file"; }
};
struct NullLogger : Logger {
    std::string name() const override { return "null"; }
};

struct ServiceA {
    explicit ServiceA(std::shared_ptr<Logger> l) : log(std::move(l)) {}
    std::shared_ptr<Logger> log;
};
struct ServiceB {
    explicit ServiceB(std::shared_ptr<Logger> l) : log(std::move(l)) {}
    std::shared_ptr<Logger> log;
};

bool has(const std::string& h, const std::string& n) { return h.find(n) != std::string::npos; }

} // namespace

// Contextual: same dependency (Logger), different implementation per consumer.
TEST(container_contextual_binding) {
    Container c;
    c.bind_auto<FileLogger>();
    c.bind_auto<NullLogger>();
    c.bind<Logger>([](ContainerContract& cc) {
        return std::static_pointer_cast<Logger>(cc.resolve<NullLogger>()); // default
    });
    c.bind_auto<ServiceA, Logger>();
    c.bind_auto<ServiceB, Logger>();

    c.when<ServiceA>().needs<Logger>().give<FileLogger>(); // override for ServiceA only

    CHECK_EQ(c.resolve<ServiceA>()->log->name(), std::string("file"));
    CHECK_EQ(c.resolve<ServiceB>()->log->name(), std::string("null"));
}

// Tagged: resolve a whole group by tag name.
TEST(container_tagged_bindings) {
    Container c;
    c.bind_auto<FileLogger>();
    c.bind_auto<NullLogger>();
    c.tag<Logger, FileLogger, NullLogger>("loggers");

    auto loggers = c.tagged<Logger>("loggers");
    CHECK_EQ(loggers.size(), static_cast<std::size_t>(2));
    CHECK_EQ(loggers[0]->name(), std::string("file"));
    CHECK_EQ(loggers[1]->name(), std::string("null"));

    CHECK(c.tagged<Logger>("does-not-exist").empty());
}

// Scoped: memoised within a scope, fresh after forget_scoped().
TEST(container_scoped_lifetime) {
    Container c;
    c.scoped_auto<FileLogger>();

    auto a = c.resolve<FileLogger>();
    auto b = c.resolve<FileLogger>();
    CHECK_EQ(a.get(), b.get()); // same instance within the scope

    c.forget_scoped();
    auto after = c.resolve<FileLogger>();
    CHECK(after.get() != a.get()); // new instance after the scope resets
}

// Diagnostics: a typed exception whose message names the missing type.
TEST(container_unbound_reports_type_name) {
    Container c;
    std::string msg;
    bool threw = false;
    try {
        c.resolve<FileLogger>();
    } catch (const BindingResolutionException& e) {
        threw = true;
        msg = e.what();
    }
    CHECK(threw);
    CHECK(has(msg, "FileLogger"));
}

// Diagnostics: the build chain names both the missing dep and its consumer.
TEST(container_unbound_reports_build_chain) {
    Container c;
    c.bind_auto<ServiceA, Logger>(); // Logger is never bound

    std::string msg;
    try {
        c.resolve<ServiceA>();
    } catch (const BindingResolutionException& e) {
        msg = e.what();
    }
    CHECK(has(msg, "Logger"));   // the missing dependency
    CHECK(has(msg, "ServiceA")); // the consumer being built
}
