// test_livewire.cpp — server-side Livewire protocol (hydrate -> action -> re-render).
#include "test_harness.hpp"

#include <memory>
#include <string>

#include "livewire.hpp"
#include "view.hpp"

namespace {

// A classic Livewire counter, server-side.
class Counter : public Component {
public:
    Counter() {
        state["count"] = "0";
        on("increment", [](Component& c, const std::string&) {
            c.state["count"] = std::to_string(std::stoi(c.state["count"]) + 1);
        });
        on("add", [](Component& c, const std::string& arg) {
            c.state["count"] = std::to_string(std::stoi(c.state["count"]) + std::stoi(arg));
        });
    }
    std::string name() const override { return "counter"; }
    std::string render() const override {
        View v;
        v.set("count", state.at("count"));
        return ::render("<span>{{ count }}</span>", v); // ::render = the free Blade function
    }
};

// A parent that nests two independent counters.
class Panel : public Component {
public:
    std::string name() const override { return "panel"; }
    std::string render() const override {
        return "<div>" + mount_child("counter", "c1") + mount_child("counter", "c2") + "</div>";
    }
};

// Records each lifecycle hook into its own state so order is observable.
class Lifecycle : public Component {
public:
    Lifecycle() { state["log"] = ""; on("ping", [](Component&, const std::string&) {}); }
    void mount() override { state["log"] += "mount;"; }
    void hydrate() override { state["log"] += "hydrate;"; }
    void dehydrate() override { state["log"] += "dehydrate;"; }
    std::string name() const override { return "lc"; }
    std::string render() const override { return state.at("log"); }
};

bool has(const std::string& h, const std::string& n) { return h.find(n) != std::string::npos; }

Livewire app() {
    Livewire lw;
    lw.component("counter", [] { return std::make_unique<Counter>(); });
    lw.component("panel", [] { return std::make_unique<Panel>(); });
    lw.component("lc", [] { return std::make_unique<Lifecycle>(); });
    return lw;
}

} // namespace

TEST(livewire_mount_embeds_state_and_html) {
    auto html = app().mount("counter");
    CHECK(has(html, "wire:id=\"counter\""));
    CHECK(has(html, "\"count\":\"0\"")); // serialized state
    CHECK(has(html, "<span>0</span>")); // rendered view
}

TEST(livewire_update_applies_action_and_rerenders) {
    UpdateRequest req;
    req.name = "counter";
    req.action = "increment";
    req.state = {{"count", "5"}}; // hydrate from "client" state
    auto res = app().update(req);
    CHECK_EQ(res.state.at("count"), std::string("6"));
    CHECK(has(res.html, "<span>6</span>"));
}

TEST(livewire_action_with_argument) {
    UpdateRequest req;
    req.name = "counter";
    req.action = "add";
    req.arg = "10";
    req.state = {{"count", "3"}};
    auto res = app().update(req);
    CHECK_EQ(res.state.at("count"), std::string("13"));
}

TEST(livewire_nested_children_mount_independently) {
    auto html = app().mount("panel");
    // Two child roots, each its own id but the same component name.
    CHECK(has(html, "wire:id=\"c1\""));
    CHECK(has(html, "wire:id=\"c2\""));
    CHECK(has(html, "wire:id=\"panel\""));       // the parent root
    CHECK(has(html, "wire:component=\"counter\"")); // children resolve the counter factory
    // Both children rendered: two distinct "<span>0</span>" occurrences.
    auto first = html.find("<span>0</span>");
    CHECK(first != std::string::npos);
    CHECK(html.find("<span>0</span>", first + 1) != std::string::npos);
}

TEST(livewire_lifecycle_hooks_fire_in_order) {
    // Initial mount: only mount() has run.
    auto html = app().mount("lc");
    CHECK(has(html, ">mount;<")); // rendered inside the wrapper

    // A subsequent request: hydrate() before the action, dehydrate() after render.
    UpdateRequest req;
    req.name = "lc";
    req.action = "ping";
    req.state = {{"log", "mount;"}};
    auto res = app().update(req);
    CHECK_EQ(res.html, std::string("mount;hydrate;"));            // render runs before dehydrate
    CHECK_EQ(res.state.at("log"), std::string("mount;hydrate;dehydrate;")); // full order
}

TEST(livewire_json_round_trip) {
    std::string out = app().handle(
        R"({"name":"counter","action":"increment","arg":"","state":{"count":"9"}})");
    CHECK(has(out, "\"count\":\"10\"")); // new state in the response
    CHECK(has(out, "<span>10</span>")); // new html in the response
}
