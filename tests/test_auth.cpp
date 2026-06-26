// test_auth.cpp — session-backed authentication.
#include "test_harness.hpp"

#include <string>

#include "auth.hpp"
#include "session.hpp"

namespace {
ArrayUserProvider provider() {
    ArrayUserProvider p;
    p.add({1, "ada", "secret"}).add({2, "bob", "hunter2"});
    return p;
}
} // namespace

TEST(auth_attempt_success_and_user) {
    auto users = provider();
    ArraySessionStore store;
    Guard guard(users, Session(store, "sid"));

    CHECK(guard.guest());
    CHECK(guard.attempt("ada", "secret"));
    CHECK(guard.check());
    auto uid = guard.id(); // hold the optional (avoid a dangling reference to a temporary)
    CHECK(uid.has_value());
    CHECK_EQ(*uid, static_cast<std::int64_t>(1));
    auto u = guard.user();
    CHECK(u.has_value());
    CHECK_EQ(u->username, std::string("ada"));
}

TEST(auth_attempt_bad_password_fails) {
    auto users = provider();
    ArraySessionStore store;
    Guard guard(users, Session(store, "sid"));

    CHECK(!guard.attempt("ada", "wrong"));
    CHECK(!guard.attempt("nobody", "secret"));
    CHECK(guard.guest());
}

TEST(auth_logout) {
    auto users = provider();
    ArraySessionStore store;
    Guard guard(users, Session(store, "sid"));
    guard.attempt("bob", "hunter2");
    CHECK(guard.check());
    guard.logout();
    CHECK(!guard.check());
}

// A guard for a later request (same session id, new guard) sees the logged-in user.
TEST(auth_persists_across_guards) {
    auto users = provider();
    ArraySessionStore store;
    Guard(users, Session(store, "sid")).attempt("ada", "secret");

    Guard later(users, Session(store, "sid"));
    CHECK(later.check());
    auto u = later.user();
    CHECK(u.has_value());
    CHECK_EQ(u->username, std::string("ada"));
}
