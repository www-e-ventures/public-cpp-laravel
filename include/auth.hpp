// auth.hpp — authentication (Laravel: Illuminate\Auth\SessionGuard + UserProvider).
//
// A UserProvider retrieves users (by id / by credentials) and validates passwords; a
// Guard ties a UserProvider to a Session, persisting the logged-in user's id under
// "auth_id". ArrayUserProvider is the in-memory default.
//
// Passwords are stored hashed through an injectable HashContract. The default is the
// dependency-free Hasher (salted FNV-1a — demo strength, documented in hash.hpp);
// production apps inject Pbkdf2Hasher (pbkdf2_hash.hpp, OpenSSL-gated) through the
// same seam: ArrayUserProvider(std::make_shared<Pbkdf2Hasher>()).
#pragma once
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "hash.hpp"
#include "session.hpp"

struct AuthUser {
    std::int64_t id = 0;
    std::string username;
    std::string password;
};

class UserProvider {
public:
    virtual ~UserProvider() = default;
    virtual std::optional<AuthUser> retrieve_by_id(std::int64_t id) const = 0;
    virtual std::optional<AuthUser> retrieve_by_credentials(const std::string& username) const = 0;
    virtual bool validate(const AuthUser& user, const std::string& password) const = 0;
};

class ArrayUserProvider : public UserProvider {
public:
    // Hashes with the dependency-free default unless a stronger HashContract is
    // injected (e.g. Pbkdf2Hasher for production passwords).
    explicit ArrayUserProvider(std::shared_ptr<const HashContract> hasher = nullptr)
        : hasher_(hasher ? std::move(hasher) : std::make_shared<const Hasher>()) {}

    // The password is hashed on the way in; AuthUser.password stores the hash.
    ArrayUserProvider& add(AuthUser user) {
        user.password = hasher_->make(user.password);
        users_.push_back(std::move(user));
        return *this;
    }
    std::optional<AuthUser> retrieve_by_id(std::int64_t id) const override {
        for (const auto& u : users_)
            if (u.id == id) return u;
        return std::nullopt;
    }
    std::optional<AuthUser> retrieve_by_credentials(const std::string& username) const override {
        for (const auto& u : users_)
            if (u.username == username) return u;
        return std::nullopt;
    }
    bool validate(const AuthUser& user, const std::string& password) const override {
        return hasher_->check(password, user.password);
    }

private:
    std::vector<AuthUser> users_;
    std::shared_ptr<const HashContract> hasher_;
};

// Session-backed guard. State persists through the Session (and its store), so a guard
// built for a later request with the same session id sees the logged-in user.
class Guard {
public:
    Guard(const UserProvider& provider, Session session)
        : provider_(&provider), session_(std::move(session)) {}

    bool attempt(const std::string& username, const std::string& password) {
        auto user = provider_->retrieve_by_credentials(username);
        if (user && provider_->validate(*user, password)) {
            login(*user);
            return true;
        }
        return false;
    }

    void login(const AuthUser& user) { session_.put("auth_id", std::to_string(user.id)); }
    void logout() { session_.forget("auth_id"); }
    bool check() const { return session_.has("auth_id"); }
    bool guest() const { return !check(); }

    std::optional<std::int64_t> id() const {
        auto v = session_.get("auth_id");
        if (!v) return std::nullopt;
        try {
            return std::stoll(*v);
        } catch (...) {
            return std::nullopt;
        }
    }
    std::optional<AuthUser> user() const {
        auto uid = id();
        return uid ? provider_->retrieve_by_id(*uid) : std::nullopt;
    }

    // The guard's session handle — the HTTP layer uses it to regenerate the id on
    // login (session fixation) and re-issue the cookie; see httpauth::attempt_login.
    Session& session() { return session_; }

private:
    const UserProvider* provider_;
    Session session_;
};
