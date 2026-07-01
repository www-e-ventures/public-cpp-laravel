// test_tokens.cpp — signed tokens (HMAC-SHA256 / JWT) + hashed personal access
// tokens. Separate exe; links OpenSSL. Covers sign/verify, expiry, tamper + wrong-
// secret rejection, the requires_ability middleware, and the ORM-backed PAT store.
#include "test_harness.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "database.hpp"
#include "http.hpp"
#include "personal_access_token.hpp"
#include "pipeline.hpp"
#include "token.hpp"

TEST(token_base64url_roundtrip_is_binary_safe) {
    std::string raw = std::string(1, '\0') + "\xff\xfe" + "data" + std::string(1, '\x80');
    CHECK_EQ(tok::detail::b64url_decode(tok::detail::b64url_encode(raw)), raw);
    std::string enc = tok::detail::b64url_encode("any binary ??? >>>");
    CHECK(enc.find('+') == std::string::npos); // URL-safe alphabet, no padding
    CHECK(enc.find('/') == std::string::npos);
    CHECK(enc.find('=') == std::string::npos);
}

TEST(token_sign_verify_roundtrip) {
    tok::Claims c;
    c.sub = "alice";
    c.abilities = {"post", "room:library"};
    c.iat = 1000;
    c.exp = 0; // never expires
    auto v = tok::verify(tok::sign(c, "s3cret"), "s3cret");
    CHECK(v.has_value());
    CHECK_EQ(v->sub, std::string("alice"));
    CHECK_EQ(v->abilities.size(), static_cast<std::size_t>(2));
    CHECK(v->can("post"));
    CHECK(v->can("room:library"));
    CHECK(!v->can("delete"));
}

TEST(token_can_wildcard) {
    tok::Claims c;
    c.abilities = {"*"};
    CHECK(c.can("anything"));
}

TEST(token_rejects_tampered_and_wrong_secret) {
    tok::Claims c;
    c.sub = "bob";
    c.abilities = {"read"};
    std::string t = tok::sign(c, "right");
    CHECK(!tok::verify(t, "wrong").has_value()); // wrong secret
    std::string tampered = t;
    tampered[tampered.size() / 2] = static_cast<char>(tampered[tampered.size() / 2] ^ 0x01);
    CHECK(!tok::verify(tampered, "right").has_value());    // flipped a byte
    CHECK(!tok::verify("not.a.jwt", "right").has_value()); // garbage payload/sig
    CHECK(!tok::verify("nodots", "right").has_value());    // malformed
}

TEST(token_expiry_is_checked) {
    tok::Claims c;
    c.sub = "x";
    c.exp = 500;
    std::string t = tok::sign(c, "k");
    CHECK(tok::verify(t, "k", /*now=*/499).has_value());  // before exp
    CHECK(tok::verify(t, "k", /*now=*/500).has_value());  // at exp (only now > exp expires)
    CHECK(!tok::verify(t, "k", /*now=*/501).has_value()); // after exp
}

TEST(token_requires_ability_middleware) {
    const std::string secret = "mw-secret";
    tok::Claims c;
    c.sub = "u";
    c.abilities = {"post"};
    std::string good = tok::sign(c, secret);
    auto pass = [](Request&) { return Response{200, "ok"}; };

    Request r1;
    r1.headers["Authorization"] = "Bearer " + good;
    CHECK_EQ(tok::requires_ability("post", secret)(r1, pass).status, 200);

    Request r2;
    r2.headers["Authorization"] = "Bearer " + good;
    CHECK_EQ(tok::requires_ability("admin", secret)(r2, pass).status, 403); // lacks ability

    Request r3; // no Authorization header
    CHECK_EQ(tok::requires_ability("post", secret)(r3, pass).status, 403);

    Request r4;
    r4.headers["Authorization"] = "Bearer garbage";
    CHECK_EQ(tok::requires_ability("post", secret)(r4, pass).status, 403);
}

TEST(pat_issue_authenticate_revoke) {
    auto db = std::make_shared<MemoryConnection>();
    pat::Store store(db);

    auto issued = store.issue(42, "cli", {"api:read", "post"});
    CHECK(!issued.plaintext.empty());
    CHECK(issued.id > 0);

    auto rec = store.authenticate(issued.plaintext);
    CHECK(rec.has_value());
    CHECK_EQ(rec->user_id, static_cast<std::int64_t>(42));
    CHECK(rec->can("api:read"));
    CHECK(rec->can("post"));
    CHECK(!rec->can("admin"));

    CHECK(!store.authenticate("some-other-token").has_value()); // unknown token

    CHECK(store.revoke(issued.id));
    CHECK(!store.authenticate(issued.plaintext).has_value()); // revoked
}

TEST(pat_hashes_at_rest) {
    auto db = std::make_shared<MemoryConnection>();
    pat::Store store(db);
    auto issued = store.issue(1, "t", {"read"});
    auto rows = db->all("personal_access_tokens");
    CHECK_EQ(rows.size(), static_cast<std::size_t>(1));
    CHECK(rows[0].get<std::string>("token_hash") != issued.plaintext);            // never plaintext
    CHECK_EQ(rows[0].get<std::string>("token_hash"), tok::sha256_hex(issued.plaintext));
}

TEST(pat_expiry_is_checked) {
    auto db = std::make_shared<MemoryConnection>();
    pat::Store store(db);
    auto issued = store.issue(7, "short", {"read"}, /*expires_at=*/1000);
    CHECK(store.authenticate(issued.plaintext, /*now=*/999).has_value());
    CHECK(!store.authenticate(issued.plaintext, /*now=*/1001).has_value());
}

int main() { return RUN_ALL_TESTS(); }
