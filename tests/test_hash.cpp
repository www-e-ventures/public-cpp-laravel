// test_hash.cpp — password hashing.
#include "test_harness.hpp"

#include <string>

#include "hash.hpp"

TEST(hash_make_and_check) {
    Hasher h;
    std::string hashed = h.make("secret");
    CHECK(hashed != "secret");       // not stored in plaintext
    CHECK(h.check("secret", hashed)); // verifies
    CHECK(!h.check("wrong", hashed)); // rejects
}

TEST(hash_is_salted) {
    Hasher h;
    // Same password hashes differently (random salt), yet both verify.
    std::string a = h.make("pw");
    std::string b = h.make("pw");
    CHECK(a != b);
    CHECK(h.check("pw", a));
    CHECK(h.check("pw", b));
}

TEST(hash_check_rejects_garbage) {
    Hasher h;
    CHECK(!h.check("x", "not-a-valid-hash"));
}
