// todo_list.hpp — a Livewire component rendering a keyed list (exercises wire:key).
#pragma once
#include <string>
#include <vector>

#include "livewire.hpp"
#include "view.hpp"

class TodoList : public Component {
public:
    TodoList() {
        state["items"] = ""; // newline-separated
        state["draft"] = "";
        on("add", [](Component& c, const std::string&) {
            if (c.state["draft"].empty()) return;
            if (!c.state["items"].empty()) c.state["items"] += "\n";
            c.state["items"] += c.state["draft"];
            c.state["draft"] = "";
        });
    }

    std::string name() const override { return "todos"; }

    std::string render() const override {
        View v;
        for (const auto& text : split(state.at("items"))) {
            View row;
            row.set("text", text);
            v.lists["items"].push_back(row);
        }
        v.set("draft", state.at("draft"));
        return ::render(
            "<ul>@foreach(items as i)<li wire:key=\"{{ text }}\">{{ text }}</li>@endforeach</ul>"
            "<input wire:model=\"draft\" value=\"{{ draft }}\"> "
            "<button wire:click=\"add\">Add</button>",
            v);
    }

private:
    static std::vector<std::string> split(const std::string& s) {
        std::vector<std::string> out;
        std::size_t i = 0;
        while (i < s.size()) {
            std::size_t nl = s.find('\n', i);
            out.push_back(s.substr(i, nl == std::string::npos ? std::string::npos : nl - i));
            if (nl == std::string::npos) break;
            i = nl + 1;
        }
        return out;
    }
};
