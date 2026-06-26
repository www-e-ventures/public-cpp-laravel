// validation.hpp — request validation (Laravel: Illuminate\Validation\Validator).
//
// Validate a field map against pipe-delimited rule strings ("required|min:3|integer").
// Pure and header-only. Like Laravel, non-`required` rules are skipped for empty values.
// Supported rules: required, integer, boolean, email, min:n, max:n, in:a,b,c.
#pragma once
#include <map>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

class Validator {
public:
    explicit Validator(std::unordered_map<std::string, std::string> data)
        : data_(std::move(data)) {}

    // rules: ordered {field, "rule|rule:param|..."} pairs.
    bool validate(const std::vector<std::pair<std::string, std::string>>& rules) {
        errors_.clear();
        for (const auto& [field, spec] : rules) {
            std::string value = value_of(field);
            for (const auto& rule : split(spec, '|')) {
                auto [name, param] = split_param(rule);
                if (name == "required") {
                    if (value.empty()) add(field, "The " + field + " field is required.");
                    continue;
                }
                if (value.empty()) continue; // skip other rules when empty
                check(field, value, name, param);
            }
        }
        return passes();
    }

    bool passes() const { return errors_.empty(); }
    bool fails() const { return !passes(); }
    const std::map<std::string, std::vector<std::string>>& errors() const { return errors_; }

private:
    void check(const std::string& field, const std::string& value, const std::string& rule,
               const std::string& param) {
        if (rule == "integer") {
            if (!is_integer(value)) add(field, "The " + field + " must be an integer.");
        } else if (rule == "boolean") {
            if (value != "0" && value != "1" && value != "true" && value != "false")
                add(field, "The " + field + " must be true or false.");
        } else if (rule == "email") {
            auto at = value.find('@');
            if (at == std::string::npos || value.find('.', at) == std::string::npos)
                add(field, "The " + field + " must be a valid email address.");
        } else if (rule == "min") {
            if (measure(value) < to_long(param))
                add(field, "The " + field + " is too small (min " + param + ").");
        } else if (rule == "max") {
            if (measure(value) > to_long(param))
                add(field, "The " + field + " is too large (max " + param + ").");
        } else if (rule == "in") {
            bool found = false;
            for (const auto& opt : split(param, ',')) found = found || (opt == value);
            if (!found) add(field, "The selected " + field + " is invalid.");
        }
    }

    // numeric value if integer, else string length — mirrors Laravel's min/max.
    long measure(const std::string& v) const {
        return is_integer(v) ? to_long(v) : static_cast<long>(v.size());
    }

    static bool is_integer(const std::string& v) {
        std::size_t i = (!v.empty() && (v[0] == '-' || v[0] == '+')) ? 1 : 0;
        if (i == v.size()) return false;
        for (; i < v.size(); ++i)
            if (v[i] < '0' || v[i] > '9') return false;
        return true;
    }
    static long to_long(const std::string& v) {
        try {
            return std::stol(v);
        } catch (...) {
            return 0;
        }
    }
    static std::vector<std::string> split(const std::string& s, char d) {
        std::vector<std::string> out;
        std::size_t start = 0;
        while (start <= s.size()) {
            std::size_t pos = s.find(d, start);
            out.push_back(s.substr(start, pos == std::string::npos ? std::string::npos : pos - start));
            if (pos == std::string::npos) break;
            start = pos + 1;
        }
        return out;
    }
    static std::pair<std::string, std::string> split_param(const std::string& rule) {
        auto colon = rule.find(':');
        if (colon == std::string::npos) return {rule, ""};
        return {rule.substr(0, colon), rule.substr(colon + 1)};
    }
    std::string value_of(const std::string& field) const {
        auto it = data_.find(field);
        return it == data_.end() ? "" : it->second;
    }
    void add(const std::string& field, std::string msg) { errors_[field].push_back(std::move(msg)); }

    std::unordered_map<std::string, std::string> data_;
    std::map<std::string, std::vector<std::string>> errors_;
};
