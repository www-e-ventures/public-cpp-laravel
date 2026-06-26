// facades.hpp — demo facade(s)
#pragma once
#include <string>

#include "facade.hpp"
#include "user_service.hpp"

// `DB::query(...)`-style static proxy over UserService. Call sites write
// Users::find(id) with no injection plumbing — at the cost of the hidden global
// container the facade reads from. See README's facade trade-off.
class Users : public FacadeFor<UserService> {
public:
    static std::string find(const std::string& id) { return root()->find(id); }
};
