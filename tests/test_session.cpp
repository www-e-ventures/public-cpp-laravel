// test_session.cpp — session store + handle.
#include "test_harness.hpp"

#include <chrono>
#include <cstddef>
#include <string>
#include <thread>
#include <vector>

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

// --- v0.8.0 hardening: CSPRNG ids, TTL, regeneration, thread safety ----------

TEST(session_ids_are_csprng_128bit_and_unique) {
    std::string a = new_session_id();
    std::string b = new_session_id();
    CHECK_EQ(a.size(), static_cast<std::size_t>(32)); // 16 bytes hex = 128 bits
    CHECK(a != b);
    for (char c : a) CHECK((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'));
}

TEST(session_rename_moves_data_and_kills_old_id) {
    ArraySessionStore store;
    store.put("old", "k", "v");
    CHECK(store.rename("old", "new"));
    CHECK(!store.get("old", "k").has_value()); // the old id no longer answers
    auto v = store.get("new", "k");
    CHECK(v.has_value());
    CHECK_EQ(*v, std::string("v"));
    CHECK(!store.rename("gone", "x")); // unknown session: nothing to move
}

TEST(session_regenerate_id_swaps_to_fresh_id) {
    ArraySessionStore store;
    Session s(store, "planted"); // an id an attacker could have planted pre-login
    s.put("auth_id", "1");
    std::string fresh = s.regenerate_id();
    CHECK(fresh != std::string("planted"));
    CHECK_EQ(s.id(), fresh);                        // the handle follows the new id
    CHECK(s.has("auth_id"));                        // data moved with it
    CHECK(!store.has("planted", "auth_id"));        // the planted id is dead
}

TEST(session_ttl_expires_idle_sessions) {
    ArraySessionStore store(std::chrono::milliseconds(30));
    store.put("sid", "k", "v");
    CHECK(store.get("sid", "k").has_value()); // fresh: still there
    std::this_thread::sleep_for(std::chrono::milliseconds(60));
    CHECK(!store.get("sid", "k").has_value()); // idle past the TTL: expired
}

TEST(session_ttl_refreshes_on_access) {
    ArraySessionStore store(std::chrono::milliseconds(80));
    store.put("sid", "k", "v");
    for (int i = 0; i < 4; ++i) { // keep touching within the TTL
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        CHECK(store.get("sid", "k").has_value());
    }
}

TEST(session_gc_purges_expired_sessions) {
    ArraySessionStore store(std::chrono::milliseconds(20));
    store.put("a", "k", "v");
    store.put("b", "k", "v");
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    store.gc();
    store.put("c", "k", "v");
    CHECK(!store.get("a", "k").has_value());
    CHECK(!store.get("b", "k").has_value());
    CHECK(store.get("c", "k").has_value());
}

// The store is shared across HttpServer worker threads; concurrent writers to the
// same store (different sessions) must not race. Run under -fsanitize=thread to
// see the pre-mutex version fail; here we settle for "doesn't crash, all data lands".
TEST(session_store_is_thread_safe) {
    ArraySessionStore store;
    std::vector<std::thread> pool;
    for (int t = 0; t < 4; ++t) {
        pool.emplace_back([&store, t] {
            std::string sid = "sid-" + std::to_string(t);
            for (int i = 0; i < 200; ++i) {
                store.put(sid, "k" + std::to_string(i), "v");
                store.get(sid, "k" + std::to_string(i));
                store.has(sid, "k0");
            }
        });
    }
    for (auto& th : pool) th.join();
    for (int t = 0; t < 4; ++t) {
        auto v = store.get("sid-" + std::to_string(t), "k199");
        CHECK(v.has_value());
    }
}
