// container_contract.hpp — the container boundary
// Laravel: Illuminate\Contracts\Container\Container
//
// The honest C++ mapping of a DI container contract. Laravel's interface keys on
// string/class names resolved by reflection; we have no reflection, so the *virtual*
// surface is type-erased (keyed by std::type_index, values as std::any). The
// ergonomic template API (bind<T>/resolve<T>/bind_auto<...>, plus contextual/tagged/
// scoped helpers) is non-virtual sugar layered on those primitives, so it's available
// through a ContainerContract& too.
#pragma once
#include <any>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <typeindex>
#include <utility>
#include <vector>

// Laravel: Illuminate\Contracts\Container\BindingResolutionException.
class BindingResolutionException : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

class ContainerContract {
public:
    virtual ~ContainerContract() = default;

    // A factory receives the container (as the contract) and yields a type-erased value.
    using Factory = std::function<std::any(ContainerContract&)>;

    // ── Type-erased primitives: the actual polymorphic surface ──
    virtual void bind_erased(std::type_index key, Factory factory) = 0;
    virtual void singleton_erased(std::type_index key, Factory factory) = 0;
    virtual void scoped_erased(std::type_index key, Factory factory) = 0;
    virtual void instance_erased(std::type_index key, std::any obj) = 0;
    virtual std::any make_erased(std::type_index key) = 0;
    virtual bool bound_erased(std::type_index key) const = 0;
    virtual void forget_scoped() = 0; // reset scoped instances (e.g. per request)

    virtual void bind_contextual_erased(std::type_index consumer, std::type_index dep,
                                        Factory resolver) = 0;
    virtual void tag_erased(const std::string& tag, Factory resolver) = 0;
    virtual std::vector<std::any> tagged_erased(const std::string& tag) = 0;

    // ── Resolution-context stack (shared base state; powers contextual bindings
    //     and the dependency chain in error messages) ──
    void push_building(std::type_index t) { building_.push_back(t); }
    void pop_building() {
        if (!building_.empty()) building_.pop_back();
    }

    // RAII helper: marks `t` as the type currently under construction.
    class ContextGuard {
    public:
        ContextGuard(ContainerContract* c, std::type_index t) : c_(c) { c_->push_building(t); }
        ~ContextGuard() { c_->pop_building(); }
        ContextGuard(const ContextGuard&) = delete;
        ContextGuard& operator=(const ContextGuard&) = delete;

    private:
        ContainerContract* c_;
    };
    ContextGuard enter_context(std::type_index t) { return ContextGuard(this, t); }

    // ── Template sugar: non-virtual, delegating to the primitives ──
    template <typename T>
    void bind(std::function<std::shared_ptr<T>(ContainerContract&)> factory) {
        bind_erased(std::type_index(typeid(T)),
                    [factory](ContainerContract& c) -> std::any { return factory(c); });
    }

    template <typename T>
    void singleton(std::function<std::shared_ptr<T>(ContainerContract&)> factory) {
        singleton_erased(std::type_index(typeid(T)),
                         [factory](ContainerContract& c) -> std::any { return factory(c); });
    }

    // Like singleton, but cleared by forget_scoped() (cf. Laravel's scoped()).
    template <typename T>
    void scoped(std::function<std::shared_ptr<T>(ContainerContract&)> factory) {
        scoped_erased(std::type_index(typeid(T)),
                      [factory](ContainerContract& c) -> std::any { return factory(c); });
    }

    template <typename T>
    void instance(std::shared_ptr<T> obj) {
        instance_erased(std::type_index(typeid(T)), std::any(std::move(obj)));
    }

    template <typename T>
    std::shared_ptr<T> resolve() {
        return std::any_cast<std::shared_ptr<T>>(make_erased(std::type_index(typeid(T))));
    }

    template <typename T>
    bool bound() const {
        return bound_erased(std::type_index(typeid(T)));
    }

    // Autowiring: resolve each Dep and forward it to T's constructor.
    // enter_context marks T while its deps resolve — enables contextual bindings and
    // the build-chain in diagnostics.
    template <typename T, typename... Deps>
    void bind_auto() {
        bind<T>([](ContainerContract& c) -> std::shared_ptr<T> {
            [[maybe_unused]] auto guard = c.enter_context(std::type_index(typeid(T)));
            return std::make_shared<T>(c.resolve<Deps>()...);
        });
    }

    template <typename T, typename... Deps>
    void singleton_auto() {
        singleton<T>([](ContainerContract& c) -> std::shared_ptr<T> {
            [[maybe_unused]] auto guard = c.enter_context(std::type_index(typeid(T)));
            return std::make_shared<T>(c.resolve<Deps>()...);
        });
    }

    template <typename T, typename... Deps>
    void scoped_auto() {
        scoped<T>([](ContainerContract& c) -> std::shared_ptr<T> {
            [[maybe_unused]] auto guard = c.enter_context(std::type_index(typeid(T)));
            return std::make_shared<T>(c.resolve<Deps>()...);
        });
    }

    // ── Contextual bindings: when<A>().needs<I>().give<Impl>() ──
    template <typename Dep>
    class NeedsClause {
    public:
        NeedsClause(ContainerContract& c, std::type_index consumer) : c_(c), consumer_(consumer) {}
        template <typename Impl>
        void give() {
            c_.bind_contextual_erased(
                consumer_, std::type_index(typeid(Dep)),
                [](ContainerContract& c) -> std::any {
                    return std::static_pointer_cast<Dep>(c.resolve<Impl>());
                });
        }

    private:
        ContainerContract& c_;
        std::type_index consumer_;
    };

    template <typename Consumer>
    class WhenClause {
    public:
        explicit WhenClause(ContainerContract& c) : c_(c) {}
        template <typename Dep>
        NeedsClause<Dep> needs() {
            return NeedsClause<Dep>(c_, std::type_index(typeid(Consumer)));
        }

    private:
        ContainerContract& c_;
    };

    template <typename Consumer>
    WhenClause<Consumer> when() {
        return WhenClause<Consumer>(*this);
    }

    // ── Tagged bindings: tag<Base, Impls...>("name"); tagged<Base>("name") ──
    template <typename Base, typename... Impls>
    void tag(const std::string& name) {
        (tag_erased(name,
                    [](ContainerContract& c) -> std::any {
                        return std::static_pointer_cast<Base>(c.resolve<Impls>());
                    }),
         ...);
    }

    template <typename Base>
    std::vector<std::shared_ptr<Base>> tagged(const std::string& name) {
        std::vector<std::shared_ptr<Base>> out;
        for (auto& a : tagged_erased(name))
            out.push_back(std::any_cast<std::shared_ptr<Base>>(a));
        return out;
    }

protected:
    std::vector<std::type_index> building_; // resolution stack, bottom -> top
};
