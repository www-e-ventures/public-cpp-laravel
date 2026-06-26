// view.hpp — a Blade-lite template engine (Laravel: Illuminate\View / Blade).
//
// Interpolation: {{ x }} (HTML-escaped), {!! x !!} (raw).
// Control:       @if(x)...@else...@endif, @foreach(list as item)...@endforeach.
// Composition:   @include('name'), and layout inheritance via @extends('layout')
//                + @section('name')...@endsection filling the layout's @yield('name').
// Directives nest. @include/@extends resolve named templates from a `Views` registry.
// Header-only.
#pragma once
#include <algorithm>
#include <cstddef>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

struct View {
    std::unordered_map<std::string, std::string> vars;
    std::unordered_map<std::string, std::vector<View>> lists;

    View& set(const std::string& key, const std::string& value) {
        vars[key] = value;
        return *this;
    }
    std::string var(const std::string& key) const {
        auto it = vars.find(key);
        return it == vars.end() ? std::string{} : it->second;
    }
};

namespace blade_detail {

using PartialResolver = std::function<std::string(const std::string&)>;
using Sections = std::unordered_map<std::string, std::string>;

inline std::string escape(const std::string& s) {
    std::string o;
    for (char c : s) {
        switch (c) {
            case '&': o += "&amp;"; break;
            case '<': o += "&lt;"; break;
            case '>': o += "&gt;"; break;
            case '"': o += "&quot;"; break;
            case '\'': o += "&#39;"; break;
            default: o += c;
        }
    }
    return o;
}

inline std::string trim(const std::string& s) {
    std::size_t b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return "";
    std::size_t e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

inline bool truthy(const std::string& v) { return !v.empty() && v != "0" && v != "false"; }

// Strip surrounding quotes/space from a directive argument, e.g. ('content') -> content.
inline std::string unquote(std::string s) {
    s = trim(s);
    if (s.size() >= 2 && (s.front() == '"' || s.front() == '\'') && s.back() == s.front())
        s = s.substr(1, s.size() - 2);
    return s;
}

// Index of the close tag matching an already-opened block (nesting-aware). `from` is
// the first index after the opening directive.
inline std::size_t matching_end(const std::string& t, std::size_t from, const std::string& open,
                                const std::string& close) {
    int depth = 1;
    std::size_t i = from;
    while (i < t.size()) {
        std::size_t o = t.find(open, i);
        std::size_t c = t.find(close, i);
        if (c == std::string::npos) return std::string::npos;
        if (o != std::string::npos && o < c) {
            ++depth;
            i = o + open.size();
        } else {
            if (--depth == 0) return c;
            i = c + close.size();
        }
    }
    return std::string::npos;
}

// Position of the top-level @else within an @if body [from, end), or npos.
inline std::size_t find_else(const std::string& t, std::size_t from, std::size_t end) {
    int depth = 0;
    std::size_t i = from;
    while (i < end) {
        std::size_t fi = t.find("@if(", i);
        std::size_t fe = t.find("@endif", i);
        std::size_t el = t.find("@else", i);
        std::size_t n = std::min(fi, std::min(fe, el));
        if (n == std::string::npos || n >= end) return std::string::npos;
        if (n == fi) {
            ++depth;
            i = fi + 4;
        } else if (n == fe) {
            --depth;
            i = fe + 6;
        } else { // @else
            if (depth == 0) return el;
            i = el + 5;
        }
    }
    return std::string::npos;
}

std::string render_impl(const std::string& tpl, const View& ctx, const PartialResolver& partials,
                        const Sections* sections);

// Collect @section('name')...@endsection blocks (rendered) from a child template.
inline Sections collect_sections(const std::string& child, const View& ctx,
                                 const PartialResolver& partials) {
    Sections out;
    std::size_t p = 0;
    while ((p = child.find("@section(", p)) != std::string::npos) {
        std::size_t close = child.find(')', p);
        std::string name = unquote(child.substr(p + 9, close - (p + 9)));
        std::size_t body = close + 1;
        std::size_t end = matching_end(child, body, "@section(", "@endsection");
        out[name] = render_impl(child.substr(body, end - body), ctx, partials, nullptr);
        p = end + std::string("@endsection").size();
    }
    return out;
}

inline std::string render_impl(const std::string& tpl, const View& ctx,
                               const PartialResolver& partials, const Sections* sections) {
    // Layout inheritance: a child that @extends a layout contributes only its sections;
    // we render the layout with those sections filling its @yields.
    std::size_t ext = tpl.find("@extends(");
    if (ext != std::string::npos) {
        std::size_t close = tpl.find(')', ext);
        std::string layout = unquote(tpl.substr(ext + 9, close - (ext + 9)));
        Sections secs = collect_sections(tpl, ctx, partials);
        return render_impl(partials(layout), ctx, partials, &secs);
    }

    std::string out;
    std::size_t i = 0;
    auto earliest = [](std::initializer_list<std::size_t> xs) {
        std::size_t m = std::string::npos;
        for (std::size_t x : xs)
            if (x < m) m = x;
        return m;
    };

    while (i < tpl.size()) {
        std::size_t p_raw = tpl.find("{!!", i);
        std::size_t p_int = tpl.find("{{", i);
        std::size_t p_if = tpl.find("@if(", i);
        std::size_t p_for = tpl.find("@foreach(", i);
        std::size_t p_inc = tpl.find("@include(", i);
        std::size_t p_yield = tpl.find("@yield(", i);
        std::size_t p_sec = tpl.find("@section(", i);
        std::size_t next = earliest({p_raw, p_int, p_if, p_for, p_inc, p_yield, p_sec});

        if (next == std::string::npos) {
            out += tpl.substr(i);
            break;
        }
        out += tpl.substr(i, next - i);

        if (next == p_raw) {
            std::size_t e = tpl.find("!!}", next);
            out += ctx.var(trim(tpl.substr(next + 3, e - (next + 3))));
            i = e + 3;
        } else if (next == p_int) {
            std::size_t e = tpl.find("}}", next);
            out += escape(ctx.var(trim(tpl.substr(next + 2, e - (next + 2)))));
            i = e + 2;
        } else if (next == p_if) {
            std::size_t cond_end = tpl.find(')', next);
            std::string key = trim(tpl.substr(next + 4, cond_end - (next + 4)));
            std::size_t body = cond_end + 1;
            std::size_t end = matching_end(tpl, body, "@if(", "@endif");
            std::size_t els = find_else(tpl, body, end);
            std::string then_part = tpl.substr(body, (els == std::string::npos ? end : els) - body);
            std::string else_part =
                els == std::string::npos ? "" : tpl.substr(els + 5, end - (els + 5));
            out += render_impl(truthy(ctx.var(key)) ? then_part : else_part, ctx, partials, sections);
            i = end + std::string("@endif").size();
        } else if (next == p_for) {
            std::size_t cond_end = tpl.find(')', next);
            std::string spec = trim(tpl.substr(next + 9, cond_end - (next + 9)));
            std::string list = trim(spec.substr(0, spec.find(" as ")));
            std::size_t body = cond_end + 1;
            std::size_t end = matching_end(tpl, body, "@foreach(", "@endforeach");
            std::string inner = tpl.substr(body, end - body);
            auto it = ctx.lists.find(list);
            if (it != ctx.lists.end()) {
                for (const View& item : it->second) {
                    View merged;
                    merged.vars = ctx.vars;
                    for (const auto& kv : item.vars) merged.vars[kv.first] = kv.second;
                    merged.lists = item.lists;
                    out += render_impl(inner, merged, partials, sections);
                }
            }
            i = end + std::string("@endforeach").size();
        } else if (next == p_inc) {
            std::size_t close = tpl.find(')', next);
            std::string name = unquote(tpl.substr(next + 9, close - (next + 9)));
            out += render_impl(partials(name), ctx, partials, sections);
            i = close + 1;
        } else if (next == p_yield) {
            std::size_t close = tpl.find(')', next);
            std::string name = unquote(tpl.substr(next + 7, close - (next + 7)));
            if (sections) {
                auto it = sections->find(name);
                if (it != sections->end()) out += it->second;
            }
            i = close + 1;
        } else { // @section in a non-extends context: render its body inline
            std::size_t close = tpl.find(')', next);
            std::size_t body = close + 1;
            std::size_t end = matching_end(tpl, body, "@section(", "@endsection");
            out += render_impl(tpl.substr(body, end - body), ctx, partials, sections);
            i = end + std::string("@endsection").size();
        }
    }
    return out;
}

} // namespace blade_detail

// Render a standalone template (no @include/@extends resolution).
inline std::string render(const std::string& tpl, const View& ctx) {
    return blade_detail::render_impl(
        tpl, ctx, [](const std::string&) { return std::string{}; }, nullptr);
}

// A registry of named templates, enabling @include and @extends.
class Views {
public:
    Views& define(const std::string& name, std::string tpl) {
        templates_[name] = std::move(tpl);
        return *this;
    }
    std::string get(const std::string& name) const {
        auto it = templates_.find(name);
        return it == templates_.end() ? std::string{} : it->second;
    }
    std::string render(const std::string& name, const View& ctx) const {
        return blade_detail::render_impl(
            get(name), ctx, [this](const std::string& n) { return get(n); }, nullptr);
    }

private:
    std::unordered_map<std::string, std::string> templates_;
};
