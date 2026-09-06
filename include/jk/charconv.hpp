#pragma once
#include <jk/config.hpp>

#include <cstdio>
#include <cstdlib>
#include <optional>
#include <string>
#include <version>

#include <string_view>

/**
 * \file charconv.hpp
 *
 * Number parsing and printing, deferring to libossia's helpers when jk is
 * built inside score and standing alone otherwise.
 *
 * ossia::parse_strict is exactly what jq's `tonumber` wants: it rejects
 * trailing garbage, so "12x" fails rather than silently becoming 12. Both
 * arms guard on __cpp_lib_to_chars, because libstdc++ shipped the integer
 * overloads of from_chars long before the floating-point ones.
 */
#if __has_include(<ossia/detail/parse_strict.hpp>)
#include <ossia/detail/parse_strict.hpp>
#define JK_HAS_OSSIA_CHARCONV 1
#endif

#if defined(__cpp_lib_to_chars)
#include <charconv>
#endif

namespace jk
{

//! Parse a complete number, rejecting anything left over.
[[nodiscard]] inline std::optional<double> parse_number(std::string_view s)
{
#if defined(JK_HAS_OSSIA_CHARCONV)
  return ossia::parse_strict<double>(s);
#elif defined(__cpp_lib_to_chars)
  double out{};
  const auto begin = s.data();
  const auto end = s.data() + s.size();
  const auto [ptr, ec] = std::from_chars(begin, end, out);
  if (ec != std::errc{} || ptr != end)
    return std::nullopt;
  return out;
#else
  // strtod is the only portable fallback; it also accepts leading space and
  // hex, so the result is checked against the whole input.
  if (s.empty())
    return std::nullopt;
  const config::string tmp{s.data(), s.size()};
  char* last{};
  const double out = std::strtod(tmp.c_str(), &last);
  if (last != tmp.c_str() + tmp.size())
    return std::nullopt;
  return out;
#endif
}

//! Append the shortest representation that reads back as the same double,
//! which is the form jq emits.
template <typename Traits, typename Allocator>
inline void
append_shortest(double d, std::basic_string<char, Traits, Allocator>& out)
{
#if defined(__cpp_lib_to_chars)
  char buf[40];
  const auto res = std::to_chars(buf, buf + sizeof(buf), d);
  out.append(buf, res.ptr - buf);
#else
  // %.17g always round-trips but is often longer than necessary; try the
  // shorter precisions first and keep the first that reads back identically.
  char buf[40];
  for (int prec = 15; prec <= 17; prec++)
  {
    const auto n = std::snprintf(buf, sizeof(buf), "%.*g", prec, d);
    if (n > 0 && std::strtod(buf, nullptr) == d)
    {
      out.append(buf, n);
      return;
    }
  }
  const auto n = std::snprintf(buf, sizeof(buf), "%.17g", d);
  out.append(buf, n > 0 ? n : 0);
#endif
}

}
