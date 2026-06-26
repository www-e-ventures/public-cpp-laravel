// demo_model.hpp — a model with NO hand-written mapper. genmodel generates it.
#pragma once
#include <cstdint>
#include <string>

struct Widget {
    std::int64_t id = 0;
    std::string name;
    bool active = false;
    std::int64_t quantity = 0;
};
