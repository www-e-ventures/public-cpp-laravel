// test_crypto.cpp — PBKDF2 hasher (separate exe; links OpenSSL).
#include "test_harness.hpp"

#include <cstddef>
#include <memory>
#include <string>

#include "auth.hpp"
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

// The production wiring for passwords: Pbkdf2Hasher through ArrayUserProvider's
// injectable-hasher seam (auth.hpp). The dependency-free default stays FNV (demo);
// any app that links crypto gets real KDF hashing with this one line.
TEST(crypto_pbkdf2_injects_into_user_provider) {
    ArrayUserProvider users(std::make_shared<Pbkdf2Hasher>(1000)); // low rounds: test speed
    users.add({1, "ada", "secret"});

    auto u = users.retrieve_by_credentials("ada");
    CHECK(u.has_value());
    CHECK_EQ(u->password.rfind("pbkdf2$", 0), static_cast<std::size_t>(0)); // KDF format
    CHECK(users.validate(*u, "secret"));
    CHECK(!users.validate(*u, "wrong"));
}

int main() { return RUN_ALL_TESTS(); }
