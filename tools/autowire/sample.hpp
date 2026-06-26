// sample.hpp — a self-contained model for the autowire demo (no app dependency).
// genmodel/autowire parse this; verify_driver resolves the result.
#pragma once
#include <memory>
#include <string>
#include <utility>

struct SampleService {
    std::string ping() const { return "pong"; }
};

struct SampleController {
    explicit SampleController(std::shared_ptr<SampleService> svc) : svc(std::move(svc)) {}
    std::shared_ptr<SampleService> svc;
};
