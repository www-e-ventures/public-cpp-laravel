// config.hpp — configuration + .env loading (Laravel: Illuminate\Config\Repository + env()).
//
// A flat key/value store (dot-notation keys by convention, e.g. "app.name") with typed
// getters, plus a pure .env parser. Header-only; register a Config as a container
// singleton in your app and read it where needed.
#pragma once
#include <string>
#include <unordered_map>

// Parse .env text: KEY=VALUE lines, '#' comments, blank lines ignored, surrounding
// quotes stripped. Pure (no I/O) so it's easy to test.
inline std::unordered_map<std::string, std::string> parse_env(const std::string& text) {
    std::unordered_map<std::string, std::string> out;
    std::size_t i = 0;
    while (i < text.size()) {
        std::size_t nl = text.find('\n', i);
        std::string line = text.substr(i, nl == std::string::npos ? std::string::npos : nl - i);
        i = (nl == std::string::npos) ? text.size() : nl + 1;

        std::size_t b = line.find_first_not_of(" \t\r");
        if (b == std::string::npos || line[b] == '#') continue;
        std::size_t eq = line.find('=', b);
        if (eq == std::string::npos) continue;

        auto trim = [](std::string s) {
            std::size_t x = s.find_first_not_of(" \t\r");
            if (x == std::string::npos) return std::string{};
            std::size_t y = s.find_last_not_of(" \t\r");
            return s.substr(x, y - x + 1);
        };
        std::string key = trim(line.substr(b, eq - b));
        std::string val = trim(line.substr(eq + 1));
        if (val.size() >= 2 && (val.front() == '"' || val.front() == '\'') && val.back() == val.front())
            val = val.substr(1, val.size() - 2);
        if (!key.empty()) out[key] = val;
    }
    return out;
}

class Config {
public:
    void set(const std::string& key, std::string value) { items_[key] = std::move(value); }

    bool has(const std::string& key) const { return items_.find(key) != items_.end(); }

    std::string get(const std::string& key, const std::string& def = "") const {
        auto it = items_.find(key);
        return it == items_.end() ? def : it->second;
    }

    long get_int(const std::string& key, long def = 0) const {
        auto it = items_.find(key);
        if (it == items_.end()) return def;
        try {
            return std::stol(it->second);
        } catch (...) {
            return def;
        }
    }

    bool get_bool(const std::string& key, bool def = false) const {
        auto it = items_.find(key);
        if (it == items_.end()) return def;
        const std::string& v = it->second;
        return v == "1" || v == "true" || v == "yes" || v == "on";
    }

    // Bulk-load (e.g. from parse_env). Existing keys are overwritten.
    void merge(const std::unordered_map<std::string, std::string>& items) {
        for (const auto& kv : items) items_[kv.first] = kv.second;
    }

private:
    std::unordered_map<std::string, std::string> items_;
};
