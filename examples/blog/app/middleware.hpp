// middleware.hpp — demo middleware
#pragma once
#include <string>

#include "pipeline.hpp"

// Logs request/response around the rest of the pipeline.
Response logging_mw(Request& req, Next next);

// Rejects with 401 unless the Authorization header matches `expected`.
Middleware require_token(const std::string& expected);
