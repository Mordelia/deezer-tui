#pragma once

#include <optional>
#include <string>

#include "api/auth.hpp"

namespace app {

std::optional<deezer::Session> open_session(std::string* format,
                                            std::string* error);

}
