#pragma once

#include <jk/action_handler.hpp>

#include <optional>

#include <string_view>

namespace jk
{
std::optional<actions::handlers> parse(std::string_view str);
}
