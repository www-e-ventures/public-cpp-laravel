// livewire.hpp — server-side reactive components (cpp-livewire; Laravel: Livewire).
//
// A Component owns string-keyed public `state` and named `actions`. Lifecycle per the
// Livewire protocol: the browser holds the rendered HTML + serialized state; when an
// action fires it POSTs {name, action, arg, state}; the server rehydrates the component
// from that state, runs the action, re-renders the Blade view, and returns {html, state}
// for the client to swap in. This header is the SERVER half (pure + testable); the JS
// client + app wiring live in the example app.
#pragma once
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>

#include "view.hpp"

// ── A minimal JSON for the wire format (flat string-keyed objects only) ──
namespace lwjson {

inline std::string esc(const std::string& s) {
    std::string o;
    for (char c : s) {
        switch (c) {
            case '"': o += "\\\""; break;
            case '\\': o += "\\\\"; break;
            case '\n': o += "\\n"; break;
            case '\r': o += "\\r"; break;
            case '\t': o += "\\t"; break;
            default: o += c;
        }
    }
    return o;
}

inline std::string object(const std::unordered_map<std::string, std::string>& m) {
    std::string o = "{";
    bool first = true;
    for (const auto& kv : m) {
        if (!first) o += ",";
        first = false;
        o += "\"" + esc(kv.first) + "\":\"" + esc(kv.second) + "\"";
    }
    return o + "}";
}

// Read a top-level string field ("key":"value").
inline std::string field(const std::string& s, const std::string& key) {
    std::string k = "\"" + key + "\"";
    auto p = s.find(k);
    if (p == std::string::npos) return "";
    p = s.find(':', p + k.size());
    if (p == std::string::npos) return "";
    p = s.find('"', p);
    if (p == std::string::npos) return "";
    std::string out;
    for (++p; p < s.size() && s[p] != '"'; ++p) {
        if (s[p] == '\\' && p + 1 < s.size()) {
            char n = s[++p];
            out += (n == 'n' ? '\n' : n == 't' ? '\t' : n);
        } else {
            out += s[p];
        }
    }
    return out;
}

// Read a nested object field ("key":{...}) as raw text including braces.
inline std::string object_field(const std::string& s, const std::string& key) {
    std::string k = "\"" + key + "\"";
    auto p = s.find(k);
    if (p == std::string::npos) return "{}";
    p = s.find('{', p);
    if (p == std::string::npos) return "{}";
    int depth = 0;
    for (std::size_t i = p; i < s.size(); ++i) {
        if (s[i] == '{') ++depth;
        else if (s[i] == '}' && --depth == 0) return s.substr(p, i - p + 1);
    }
    return "{}";
}

// Parse a flat object {"k":"v",...} into a map (string values only).
inline std::unordered_map<std::string, std::string> parse_object(const std::string& s) {
    std::unordered_map<std::string, std::string> out;
    std::size_t i = 0;
    auto read_string = [&](std::size_t& pos) {
        std::string r;
        for (++pos; pos < s.size() && s[pos] != '"'; ++pos) {
            if (s[pos] == '\\' && pos + 1 < s.size()) {
                char n = s[++pos];
                r += (n == 'n' ? '\n' : n == 't' ? '\t' : n);
            } else {
                r += s[pos];
            }
        }
        return r;
    };
    while (i < s.size()) {
        std::size_t q = s.find('"', i);
        if (q == std::string::npos) break;
        std::string key = read_string(q); // q now at closing quote
        std::size_t colon = s.find(':', q);
        if (colon == std::string::npos) break;
        std::size_t v = s.find('"', colon);
        if (v == std::string::npos) break;
        std::string val = read_string(v);
        out[key] = val;
        i = v + 1;
    }
    return out;
}

} // namespace lwjson

class Livewire; // owns components; lets a parent mount children

class Component {
public:
    virtual ~Component() = default;
    virtual std::string name() const = 0;
    virtual std::string render() const = 0; // HTML, typically via ::render(template, View)

    // Lifecycle hooks (Laravel: mount/hydrate/dehydrate). mount() runs once on the
    // initial render; hydrate() runs on every subsequent request after the posted
    // state is loaded but before the action; dehydrate() runs after render, just
    // before the state is serialized back to the client.
    virtual void mount() {}
    virtual void hydrate() {}
    virtual void dehydrate() {}

    using Action = std::function<void(Component&, const std::string& arg)>;
    void on(const std::string& action, Action handler) { actions_[action] = std::move(handler); }
    bool call(const std::string& action, const std::string& arg) {
        auto it = actions_.find(action);
        if (it == actions_.end()) return false;
        it->second(*this, arg);
        return true;
    }

    std::unordered_map<std::string, std::string> state; // public, hydrated each request

protected:
    // Render a child component (its own wrapper/state/actions) inside this one.
    std::string mount_child(const std::string& name, const std::string& id = "") const;

private:
    friend class Livewire;
    const Livewire* owner_ = nullptr;
    std::unordered_map<std::string, Action> actions_;
};

struct UpdateRequest {
    std::string name;
    std::string action;
    std::string arg;
    std::unordered_map<std::string, std::string> state;
};
struct UpdateResponse {
    std::string html;
    std::unordered_map<std::string, std::string> state;
};

class Livewire {
public:
    using Factory = std::function<std::unique_ptr<Component>()>;

    Livewire& component(const std::string& name, Factory f) {
        factories_[name] = std::move(f);
        return *this;
    }

    std::unique_ptr<Component> make(const std::string& name) const {
        auto it = factories_.find(name);
        if (it == factories_.end()) return nullptr;
        std::unique_ptr<Component> c = it->second();
        c->owner_ = this; // so the component can mount children
        return c;
    }

    // Initial render: a wrapper carrying the DOM id, the component name, and the
    // serialized state, plus its rendered HTML. `id` distinguishes instances of the
    // same component (nested/repeated); it defaults to the component name.
    std::string mount(const std::string& name, const std::string& id = "") const {
        auto c = make(name);
        if (!c) return "";
        c->mount(); // initial-render hook (runs once)
        const std::string dom_id = id.empty() ? name : id;
        return "<div wire:id=\"" + dom_id + "\" wire:component=\"" + name + "\" wire:state='" +
               lwjson::object(c->state) + "'>" + c->render() + "</div>";
    }

    // Apply an action to a rehydrated component and re-render (pure, testable).
    UpdateResponse update(const UpdateRequest& req) const {
        UpdateResponse res;
        auto c = make(req.name);
        if (!c) return res;
        // Hydrate: overlay the client's state onto the component's defaults, so a
        // request that omits a property keeps the constructor's default for it.
        for (const auto& kv : req.state) c->state[kv.first] = kv.second;
        c->hydrate();              // post-hydration hook
        c->call(req.action, req.arg);
        res.html = c->render();
        c->dehydrate();            // pre-serialize hook
        res.state = c->state;
        return res;
    }

    // HTTP entry: parse the request JSON, update, return {html, state} JSON.
    std::string handle(const std::string& request_json) const {
        UpdateRequest req;
        req.name = lwjson::field(request_json, "name");
        req.action = lwjson::field(request_json, "action");
        req.arg = lwjson::field(request_json, "arg");
        req.state = lwjson::parse_object(lwjson::object_field(request_json, "state"));
        UpdateResponse res = update(req);
        return "{\"html\":\"" + lwjson::esc(res.html) + "\",\"state\":" + lwjson::object(res.state) +
               "}";
    }

private:
    std::unordered_map<std::string, Factory> factories_;
};

inline std::string Component::mount_child(const std::string& name, const std::string& id) const {
    return owner_ ? owner_->mount(name, id) : std::string{};
}
