// user_service.hpp — demo domain service (resolved as a container singleton)
#pragma once
#include <string>

class UserService {
public:
    std::string find(const std::string& id) {
        if (id == "42") return R"({"id":42,"name":"Ada Lovelace"})";
        return "";
    }
};
