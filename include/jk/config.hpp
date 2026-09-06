#pragma once
#include <jk/memory.hpp>
#if __has_include(<jk_config_customization.hpp>)
#include <jk_config_customization.hpp>
#elif !defined(JK_CONFIG_CUSTOMIZATION)
#include <map>
#include <string>
#include <variant>
#include <vector>
namespace jk::config
{
namespace variant_ns = ::std;
template <typename... Args>
using variant = std::variant<Args...>;
template <typename T, typename Allocator = jk::allocator<T>>
using vector = std::vector<T, Allocator>;
template <
    typename K,
    typename V,
    typename Compare = std::less<>,
    typename Allocator = jk::allocator<std::pair<const K, V>>>
using map = std::map<K, V, Compare, Allocator>;

using string
    = std::basic_string<char, std::char_traits<char>, jk::allocator<char>>;
}
#endif
