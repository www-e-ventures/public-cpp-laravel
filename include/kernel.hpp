// kernel.hpp — HTTP kernel
// Laravel: Illuminate\Foundation\Http\Kernel (implements KernelContract)
//
// Ties container → router → global middleware → pipeline. Depends on the *contracts*
// (ContainerContract, RouterContract), not the concretes, so it can be driven against
// a fake router in tests (see tests/test_kernel_contract.cpp).
#pragma once
#include <memory>
#include <vector>

#include "container_contract.hpp"
#include "http.hpp"
#include "kernel_contract.hpp"
#include "pipeline.hpp"
#include "router_contract.hpp"
#include "service_provider.hpp"

class Kernel : public KernelContract {
public:
    Kernel(std::shared_ptr<ContainerContract> c, std::shared_ptr<RouterContract> r);

    void global_middleware(std::vector<Middleware> mw);

    // Two-phase boot: register_() on every provider, then boot() on every provider.
    void register_providers(std::vector<std::shared_ptr<ServiceProvider>> providers);

    Response handle(Request req) override;

private:
    std::shared_ptr<ContainerContract> container_;
    std::shared_ptr<RouterContract> router_;
    std::vector<Middleware> global_;
    std::vector<std::shared_ptr<ServiceProvider>> providers_;
};
