// article_controller.hpp — HTTP resource controller for the smoke-test app.
// Exercises the full stack: routing -> middleware -> container-resolved controller
// -> repository -> connection. JSON is hand-rolled and the request body is parsed
// as application/x-www-form-urlencoded — both kept dependency-free on purpose.
#pragma once
#include <cstdint>
#include <memory>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>

#include "http.hpp"
#include "models/article.hpp"
#include "view.hpp"

class ArticleController {
public:
    explicit ArticleController(std::shared_ptr<ArticleRepository> repo) : repo_(std::move(repo)) {}

    // GET /articles.html — renders a Blade-lite view.
    Response html(Request&) {
        View ctx;
        for (const auto& a : repo_->all()) {
            View row;
            row.set("title", a.title);
            ctx.lists["articles"].push_back(row);
        }
        Response r;
        r.headers["Content-Type"] = "text/html";
        r.body = render("<ul>@foreach(articles as a)<li>{{ title }}</li>@endforeach</ul>", ctx);
        return r;
    }

    // GET /articles
    Response index(Request&) {
        std::ostringstream os;
        os << "[";
        bool first = true;
        for (const auto& a : repo_->all()) {
            if (!first) os << ",";
            os << to_json(a);
            first = false;
        }
        os << "]";
        return Response::json(os.str());
    }

    // GET /articles/{id}
    Response show(Request& req) {
        auto a = repo_->find(to_int(req.route_params["id"]));
        if (!a) return Response::json(R"({"error":"not found"})", 404);
        return Response::json(to_json(*a));
    }

    // POST /articles   body: title=...&views=...&published=1
    Response store(Request& req) {
        auto form = parse_form(req.body);
        Article a;
        a.title = value(form, "title");
        a.views = to_int(value(form, "views"));
        a.published = is_truthy(value(form, "published"));
        if (a.title.empty())
            return Response::json(R"({"error":"title is required"})", 422);
        repo_->insert(a);
        return Response::json(to_json(a), 201);
    }

private:
    static std::string to_json(const Article& a) {
        std::ostringstream os;
        os << R"({"id":)" << a.id
           << R"(,"title":")" << escape(a.title) << R"(")"
           << R"(,"views":)" << a.views
           << R"(,"published":)" << (a.published ? "true" : "false") << "}";
        return os.str();
    }

    static std::string escape(const std::string& s) {
        std::string out;
        for (char c : s) {
            if (c == '"' || c == '\\') out += '\\';
            out += c;
        }
        return out;
    }

    static std::int64_t to_int(const std::string& s) {
        try {
            return s.empty() ? 0 : std::stoll(s);
        } catch (...) {
            return 0;
        }
    }

    static bool is_truthy(const std::string& s) { return s == "1" || s == "true"; }

    static std::string value(const std::unordered_map<std::string, std::string>& m,
                             const std::string& key) {
        auto it = m.find(key);
        return it == m.end() ? std::string{} : it->second;
    }

    static std::unordered_map<std::string, std::string> parse_form(const std::string& body) {
        std::unordered_map<std::string, std::string> out;
        std::size_t i = 0;
        while (i < body.size()) {
            auto amp = body.find('&', i);
            std::string pair = body.substr(i, amp == std::string::npos ? std::string::npos : amp - i);
            auto eq = pair.find('=');
            if (eq != std::string::npos) out[pair.substr(0, eq)] = pair.substr(eq + 1);
            if (amp == std::string::npos) break;
            i = amp + 1;
        }
        return out;
    }

    std::shared_ptr<ArticleRepository> repo_;
};
