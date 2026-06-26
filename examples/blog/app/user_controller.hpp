// user_controller.hpp — demo controller; depends on UserService via the container
#pragma once
#include <memory>
#include <utility>

#include "http.hpp"
#include "user_service.hpp"

class UserController {
public:
    explicit UserController(std::shared_ptr<UserService> svc) : svc_(std::move(svc)) {}

    Response show(Request& req) {
        auto id = req.route_params["id"];
        auto user = svc_->find(id);
        if (user.empty())
            return Response::json(R"({"error":"not found"})", 404);
        return Response::json(user);
    }

private:
    std::shared_ptr<UserService> svc_;
};
