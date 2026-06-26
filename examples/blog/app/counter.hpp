// counter.hpp — a Livewire counter component (the classic demo).
#pragma once
#include <memory>
#include <string>

#include "livewire.hpp"
#include "view.hpp"

class Counter : public Component {
public:
    Counter() {
        state["count"] = "0";
        state["name"] = ""; // two-way bound via wire:model
        on("increment", [](Component& c, const std::string&) {
            c.state["count"] = std::to_string(std::stoi(c.state["count"]) + 1);
        });
        on("decrement", [](Component& c, const std::string&) {
            c.state["count"] = std::to_string(std::stoi(c.state["count"]) - 1);
        });
    }

    std::string name() const override { return "counter"; }

    std::string render() const override {
        View v;
        v.set("count", state.at("count"));
        v.set("name", state.at("name"));
        v.set("greeting", state.at("name").empty() ? "stranger" : state.at("name"));
        return ::render( // ::render = the free Blade function (not Component::render)
            "<span>Count: {{ count }}</span> "
            "<button wire:click=\"decrement\">-</button> "
            "<button wire:click=\"increment\">+</button>"
            "<div><input wire:model=\"name\" value=\"{{ name }}\"> "
            "<p>Hello {{ greeting }}</p></div>"
            "<span wire:loading>updating…</span>"                       // any request
            "<span wire:loading wire:target=\"increment\">+1…</span>", // only on increment
            v);
    }
};
