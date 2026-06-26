// pipeline.hpp — middleware pipeline
// Laravel: Illuminate\Pipeline\Pipeline
//
// Each middleware: (Request&, Next) -> Response. Composed right-to-left so the
// first registered middleware is the outermost layer. A middleware short-circuits
// by returning a Response without calling next.
#pragma once
#include <functional>
#include <vector>

#include "http.hpp"

using Next = std::function<Response(Request&)>;
using Middleware = std::function<Response(Request&, Next)>;
using Handler = std::function<Response(Request&)>;

class Pipeline {
public:
    Pipeline& through(std::vector<Middleware> mws);
    Response run(Request& req, Handler dest);

private:
    std::vector<Middleware> middlewares_;
};
