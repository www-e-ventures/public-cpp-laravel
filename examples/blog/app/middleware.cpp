// middleware.cpp — see middleware.hpp
#include "middleware.hpp"

#include <iostream>

Response logging_mw(Request& req, Next next) {
    std::cerr << "[log] --> " << req.method << " " << req.path << "\n";
    Response res = next(req);
    std::cerr << "[log] <-- " << res.status << "\n";
    return res;
}

Middleware require_token(const std::string& expected) {
    return [expected](Request& req, Next next) -> Response {
        auto it = req.headers.find("Authorization");
        if (it == req.headers.end() || it->second != expected)
            return Response::json(R"({"error":"unauthorized"})", 401);
        return next(req);
    };
}
