// container.hpp — type-erased service container
// Laravel: Illuminate\Container\Container (implements ContainerContract)
//
// Implements the type-erased primitives of ContainerContract; the template API
// (bind<T>/resolve<T>/bind_auto/when/tag/scoped) is inherited from the contract.
// Type erasure is the right core here, but it can't autowire: C++ has no runtime
// reflection, so each binding lists its dependencies explicitly in bind_auto.
#pragma once
#include <any>
#include <mutex>
#include <string>
#include <typeindex>
#include <unordered_map>
#include <utility>

#include "container_contract.hpp"

#if __has_include(<cxxabi.h>)
#include <cxxabi.h>
#include <cstdlib>
// Turn a mangled typeid name into something human-readable for diagnostics.
inline std::string demangle_type(const char* name) {
    int status = 0;
    char* d = abi::__cxa_demangle(name, nullptr, nullptr, &status);
    std::string out = (status == 0 && d) ? d : name;
    std::free(d);
    return out;
}
#else
inline std::string demangle_type(const char* name) { return name; }
#endif

// Thread-safe: a recursive mutex guards all state, so the container can be shared
// across the HTTP server's worker threads. The mutex is recursive because make_erased
// invokes factories that call resolve() again on the same thread. Resolution thus
// serializes across threads (a short critical section); request work runs outside it.
class Container : public ContainerContract {
public:
    // Transient: factory runs on every resolve.
    void bind_erased(std::type_index key, Factory factory) override {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        factories_[key] = std::move(factory);
    }

    // Singleton: factory runs once, result memoised for the container's lifetime.
    void singleton_erased(std::type_index key, Factory factory) override {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        factories_[key] = [factory, this, key](ContainerContract& c) -> std::any {
            auto it = instances_.find(key);
            if (it != instances_.end()) return it->second;
            std::any made = factory(c);
            instances_[key] = made;
            return made;
        };
    }

    // Scoped: memoised like a singleton, but dropped by forget_scoped().
    void scoped_erased(std::type_index key, Factory factory) override {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        factories_[key] = [factory, this, key](ContainerContract& c) -> std::any {
            auto it = scoped_.find(key);
            if (it != scoped_.end()) return it->second;
            std::any made = factory(c);
            scoped_[key] = made;
            return made;
        };
    }

    void instance_erased(std::type_index key, std::any obj) override {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        instances_[key] = obj;
        factories_[key] = [obj](ContainerContract&) -> std::any { return obj; };
    }

    std::any make_erased(std::type_index key) override {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        // Contextual override: if the type currently being built has a specific
        // binding for `key`, use it instead of the global one.
        if (!building_.empty()) {
            auto ci = contextual_.find(building_.back());
            if (ci != contextual_.end()) {
                auto di = ci->second.find(key);
                if (di != ci->second.end()) return di->second(*this);
            }
        }

        auto it = factories_.find(key);
        if (it == factories_.end()) throw unresolved(key);
        return it->second(*this);
    }

    bool bound_erased(std::type_index key) const override {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        return factories_.count(key) > 0;
    }

    void forget_scoped() override {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        scoped_.clear();
    }

    void bind_contextual_erased(std::type_index consumer, std::type_index dep,
                                Factory resolver) override {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        contextual_[consumer][dep] = std::move(resolver);
    }

    void tag_erased(const std::string& tag, Factory resolver) override {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        tags_[tag].push_back(std::move(resolver));
    }

    std::vector<std::any> tagged_erased(const std::string& tag) override {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        std::vector<std::any> out;
        auto it = tags_.find(tag);
        if (it == tags_.end()) return out;
        out.reserve(it->second.size());
        for (auto& f : it->second) out.push_back(f(*this));
        return out;
    }

private:
    // Build a diagnostic naming the missing type and the chain being constructed.
    BindingResolutionException unresolved(std::type_index key) const {
        std::string msg = "No binding registered for [" + demangle_type(key.name()) + "]";
        if (!building_.empty()) {
            msg += " while building [";
            for (std::size_t i = 0; i < building_.size(); ++i) {
                if (i) msg += " -> ";
                msg += demangle_type(building_[i].name());
            }
            msg += "]";
        }
        return BindingResolutionException(msg);
    }

    std::unordered_map<std::type_index, Factory> factories_;
    std::unordered_map<std::type_index, std::any> instances_;
    std::unordered_map<std::type_index, std::any> scoped_;
    std::unordered_map<std::type_index, std::unordered_map<std::type_index, Factory>> contextual_;
    std::unordered_map<std::string, std::vector<Factory>> tags_;
    mutable std::recursive_mutex mutex_;
};
