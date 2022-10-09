#pragma once

#include <jk/action_handler.hpp>
#include <jk/value.hpp>

#include <optional>

namespace jk
{
std::optional<actions::handlers> parse(std::string_view str);
}
