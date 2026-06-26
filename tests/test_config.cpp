// test_config.cpp — config store + .env parsing.
#include "test_harness.hpp"

#include <string>

#include "config.hpp"

TEST(env_parsing) {
    auto env = parse_env(
        "# a comment\n"
        "APP_NAME=cpp-laravel\n"
        "\n"
        "DEBUG = true \n"
        "QUOTED=\"hello world\"\n"
        "bad line without equals\n");
    CHECK_EQ(env.at("APP_NAME"), std::string("cpp-laravel"));
    CHECK_EQ(env.at("DEBUG"), std::string("true"));         // trimmed
    CHECK_EQ(env.at("QUOTED"), std::string("hello world")); // quotes stripped
    CHECK(env.find("bad line without equals") == env.end());
}

TEST(config_get_and_defaults) {
    Config c;
    c.set("app.name", "Blog");
    CHECK(c.has("app.name"));
    CHECK_EQ(c.get("app.name"), std::string("Blog"));
    CHECK_EQ(c.get("missing", "fallback"), std::string("fallback"));
}

TEST(config_typed_getters) {
    Config c;
    c.set("app.workers", "4");
    c.set("app.debug", "true");
    CHECK_EQ(c.get_int("app.workers"), 4L);
    CHECK_EQ(c.get_int("missing", 7), 7L);
    CHECK(c.get_bool("app.debug"));
    CHECK(!c.get_bool("missing"));
}

TEST(config_merge_from_env) {
    Config c;
    c.merge(parse_env("APP_ENV=production\nCACHE=1\n"));
    CHECK_EQ(c.get("APP_ENV"), std::string("production"));
    CHECK(c.get_bool("CACHE"));
}
