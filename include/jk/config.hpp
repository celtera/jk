#pragma once
#if __has_include(<jk_config_customization.hpp>)
#include <jk_config_customization.hpp>
#elif !defined(JK_CONFIG_CUSTOMIZATION)
#include <map>
#include <string>
#include <variant>
#include <vector>
namespace jk::config
{
template <typename... Args>
using variant = std::variant<Args...>;
template <typename... Args>
using vector = std::vector<Args...>;
template <typename... Args>
using map = std::map<Args...>;

using string = std::string;
}
#endif
