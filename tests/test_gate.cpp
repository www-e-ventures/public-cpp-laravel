// test_gate.cpp — authorization gates.
#include "test_harness.hpp"

#include <memory>
#include <optional>
#include <string>

#include "auth.hpp"
#include "gate.hpp"

namespace {
const AuthUser ada{1, "ada", ""};
const AuthUser bob{2, "bob", ""};
const AuthUser admin{9, "admin", ""};

// A policy for "post": anyone may view; only the owner may update/delete.
class PostPolicy : public Policy {
public:
    bool allows(const std::string& ability, const AuthUser& u,
                const std::string& owner_id) const override {
        if (ability == "view") return true;
        if (ability == "update" || ability == "delete") return std::to_string(u.id) == owner_id;
        return false;
    }
};
} // namespace

TEST(gate_allows_owner_denies_other) {
    Gate g;
    g.define("edit-post", [](const AuthUser& u, const std::string& owner_id) {
        return std::to_string(u.id) == owner_id;
    });
    CHECK(g.allows("edit-post", ada, "1"));  // ada owns post 1
    CHECK(g.denies("edit-post", bob, "1"));  // bob does not
}

TEST(gate_undefined_ability_denies) {
    Gate g;
    CHECK(g.denies("anything", ada));
}

TEST(gate_policy_authorizes_by_model_type) {
    Gate g;
    g.policy("post", std::make_shared<PostPolicy>());

    CHECK(g.authorize("post", "view", bob, "1"));    // anyone can view
    CHECK(g.authorize("post", "update", ada, "1"));  // ada owns post 1
    CHECK(!g.authorize("post", "update", bob, "1")); // bob doesn't
    CHECK(!g.authorize("post", "publish", ada, "1")); // unknown ability -> deny
    CHECK(!g.authorize("comment", "view", ada));      // no policy for "comment" -> deny
}

TEST(gate_policy_respects_before_hook) {
    Gate g;
    g.before([](const AuthUser& u, const std::string&) -> std::optional<bool> {
        return u.username == "admin" ? std::optional<bool>(true) : std::nullopt;
    });
    g.policy("post", std::make_shared<PostPolicy>());

    CHECK(g.authorize("post", "delete", admin, "1")); // before() grants admin
    CHECK(!g.authorize("post", "delete", bob, "1"));  // before() abstains -> policy denies
}

TEST(gate_before_hook_short_circuits) {
    Gate g;
    g.before([](const AuthUser& u, const std::string&) -> std::optional<bool> {
        if (u.username == "admin") return true; // admin may do anything
        return std::nullopt;                    // otherwise: fall through
    });
    g.define("edit-post", [](const AuthUser&, const std::string&) { return false; });

    CHECK(g.allows("edit-post", admin)); // granted by before()
    CHECK(g.denies("edit-post", ada));   // before() abstained -> ability denies
}
