// dashboard.hpp — a parent component that nests two independent Counter children.
#pragma once
#include <string>

#include "livewire.hpp"

class Dashboard : public Component {
public:
    std::string name() const override { return "dashboard"; }

    std::string render() const override {
        // Each child is its own component (own id, state, actions). The morphing
        // client preserves nested [wire:id] subtrees, so a parent re-render won't
        // clobber a child's live state.
        return "<h2>Dashboard</h2>" + mount_child("counter", "counter-a") +
               mount_child("counter", "counter-b");
    }
};
