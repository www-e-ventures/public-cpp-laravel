// test_crypto.cpp — PBKDF2 hasher (separate exe; links OpenSSL).
#include "test_harness.hpp"

#include <string>

#include "pbkdf2_hash.hpp"

TEST(pbkdf2_make_and_check) {
    Pbkdf2Hasher h(10000); // fewer iterations to keep the test fast
    std::string hashed = h.make("secret");
    CHECK(hashed.rfind("pbkdf2$", 0) == 0); // encoded format
    CHECK(h.check("secret", hashed));
    CHECK(!h.check("wrong", hashed));
}

TEST(pbkdf2_is_salted) {
    Pbkdf2Hasher h(10000);
    std::string a = h.make("pw");
    std::string b = h.make("pw");
    CHECK(a != b); // random salt
    CHECK(h.check("pw", a));
    CHECK(h.check("pw", b));
}

TEST(pbkdf2_rejects_garbage) {
    Pbkdf2Hasher h(10000);
    CHECK(!h.check("x", "not-a-pbkdf2-hash"));
}

int main() { return RUN_ALL_TESTS(); }
