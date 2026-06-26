// kernel_contract.hpp — the HTTP kernel boundary
// Laravel: Illuminate\Contracts\Http\Kernel
//
// The narrow contract a client sees: hand it a Request, get a Response.
#pragma once
#include "http.hpp"

class KernelContract {
public:
    virtual ~KernelContract() = default;
    virtual Response handle(Request req) = 0;
};
