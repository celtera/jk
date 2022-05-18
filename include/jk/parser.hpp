#pragma once

#include <jk/value.hpp>
#include <jk/action_handler.hpp>
#include <optional>

namespace jk {
std::optional<actions::handlers> parse(std::string_view str);
}
