// gate.hpp — authorization gates (Laravel: Illuminate\Auth\Access\Gate).
//
// Define named abilities as callbacks over the current user (+ an optional resource
// key); check with allows()/denies(). A before() hook can short-circuit (e.g. an
// admin who may do anything). Undefined abilities deny by default.
#pragma once
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>

#include "auth.hpp"

// A policy groups a model type's abilities (Laravel: a Policy class). Implement allows()
// to decide an ability for a user against an optional resource key.
class Policy {
public:
    virtual ~Policy() = default;
    virtual bool allows(const std::string& ability, const AuthUser& user,
                        const std::string& resource) const = 0;
};

class Gate {
public:
    using Ability = std::function<bool(const AuthUser&, const std::string& resource)>;
    using BeforeHook = std::function<std::optional<bool>(const AuthUser&, const std::string& ability)>;

    Gate& define(const std::string& ability, Ability callback) {
        abilities_[ability] = std::move(callback);
        return *this;
    }
    Gate& before(BeforeHook hook) {
        before_ = std::move(hook);
        return *this;
    }

    bool allows(const std::string& ability, const AuthUser& user,
                const std::string& resource = "") const {
        if (before_) {
            auto decision = before_(user, ability);
            if (decision) return *decision; // hook had an opinion
        }
        auto it = abilities_.find(ability);
        if (it == abilities_.end()) return false; // unknown ability -> deny
        return it->second(user, resource);
    }
    bool denies(const std::string& ability, const AuthUser& user,
                const std::string& resource = "") const {
        return !allows(ability, user, resource);
    }

    // ── Policies: register a policy object for a model type, then authorize() ──
    Gate& policy(const std::string& type, std::shared_ptr<Policy> p) {
        policies_[type] = std::move(p);
        return *this;
    }

    bool authorize(const std::string& type, const std::string& ability, const AuthUser& user,
                   const std::string& resource = "") const {
        if (before_) {
            auto decision = before_(user, type + "." + ability);
            if (decision) return *decision;
        }
        auto it = policies_.find(type);
        if (it == policies_.end()) return false; // no policy -> deny
        return it->second->allows(ability, user, resource);
    }

private:
    std::unordered_map<std::string, Ability> abilities_;
    std::unordered_map<std::string, std::shared_ptr<Policy>> policies_;
    BeforeHook before_;
};
