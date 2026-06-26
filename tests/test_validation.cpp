// test_validation.cpp — the Validator rules.
#include "test_harness.hpp"

#include <string>

#include "validation.hpp"

TEST(validation_passes_when_valid) {
    Validator v({{"title", "Hello"}, {"views", "42"}, {"email", "a@b.com"}});
    bool ok = v.validate({{"title", "required|min:3"}, {"views", "integer|max:100"},
                          {"email", "required|email"}});
    CHECK(ok);
    CHECK(v.passes());
    CHECK(v.errors().empty());
}

TEST(validation_required_and_integer) {
    Validator v({{"views", "abc"}});
    CHECK(!v.validate({{"title", "required"}, {"views", "integer"}}));
    CHECK(v.fails());
    CHECK(v.errors().count("title") == 1); // missing required
    CHECK(v.errors().count("views") == 1); // not an integer
}

TEST(validation_min_max_numeric_and_length) {
    Validator v({{"age", "5"}, {"name", "ab"}});
    v.validate({{"age", "integer|min:18"}, {"name", "min:3"}});
    CHECK(v.errors().count("age") == 1);  // 5 < 18 (numeric)
    CHECK(v.errors().count("name") == 1); // length 2 < 3
}

TEST(validation_in_rule) {
    Validator ok({{"role", "admin"}});
    CHECK(ok.validate({{"role", "in:admin,editor"}}));

    Validator bad({{"role", "guest"}});
    CHECK(!bad.validate({{"role", "in:admin,editor"}}));
}

TEST(validation_skips_optional_empty) {
    // An empty, non-required field passes its other rules.
    Validator v({{"nickname", ""}});
    CHECK(v.validate({{"nickname", "min:3|email"}}));
}
