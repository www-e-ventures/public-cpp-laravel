// test_session.cpp — session store + handle.
#include "test_harness.hpp"

#include <string>

#include "session.hpp"

TEST(session_put_get_forget) {
    ArraySessionStore store;
    Session s(store, "sid-1");
    CHECK(!s.has("user"));
    s.put("user", "ada");
    CHECK(s.has("user"));
    CHECK_EQ(s.get_or("user", "?"), std::string("ada"));
    s.forget("user");
    CHECK(!s.has("user"));
}

TEST(session_isolated_by_id) {
    ArraySessionStore store;
    Session a(store, "sid-a");
    Session b(store, "sid-b");
    a.put("k", "A");
    b.put("k", "B");
    CHECK_EQ(a.get_or("k", "?"), std::string("A"));
    CHECK_EQ(b.get_or("k", "?"), std::string("B")); // separate sessions
}

TEST(session_flush_clears_everything) {
    ArraySessionStore store;
    Session s(store, "sid-1");
    s.put("a", "1");
    s.put("b", "2");
    s.flush();
    CHECK(!s.has("a"));
    CHECK(!s.has("b"));
}

// Two handles to the same id (e.g. two requests) share data through the store.
TEST(session_shared_across_handles) {
    ArraySessionStore store;
    Session(store, "sid").put("token", "xyz");
    Session again(store, "sid");
    CHECK_EQ(again.get_or("token", "?"), std::string("xyz"));
}
