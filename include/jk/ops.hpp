#pragma once
#include <jk/value.hpp>

#include <cmath>
#include <stdexcept>
#include <string>

/**
 * \file ops.hpp
 *
 * The value semantics jq defines: its type ordering, its notion of truth, and
 * the overloads of its arithmetic operators. Kept apart from actions.hpp
 * because none of it involves the generator machinery - these are pure
 * value-to-value functions, which is also what makes them straightforward to
 * test against the reference implementation.
 */
namespace jk
{

//! A jq program that hits a type error produces no output at all rather than
//! skipping the offending value, so an error has to unwind rather than be
//! swallowed locally. `?` and `//` catch it; the host catches whatever is
//! left.
struct error : std::runtime_error
{
  using std::runtime_error::runtime_error;
};

//! jq sorts types in this order: null < false < true < numbers < strings <
//! arrays < objects. Booleans come before numbers, which is worth stating
//! because the variant happens to declare them the other way round.
enum class kind
{
  null = 0,
  boolean,
  number,
  string,
  array,
  object
};

[[nodiscard]] inline kind kind_of(const value& v) noexcept
{
  if(get_if<null_t>(&v.v))
    return kind::null;
  if(get_if<int64_t>(&v.v) || get_if<double>(&v.v))
    return kind::number;
  if(get_if<bool>(&v.v))
    return kind::boolean;
  if(get_if<string_type>(&v.v))
    return kind::string;
  if(get_if<list_type>(&v.v))
    return kind::array;
  return kind::object;
}

[[nodiscard]] inline const char* type_name(const value& v) noexcept
{
  switch(kind_of(v))
  {
    case kind::null:
      return "null";
    case kind::number:
      return "number";
    case kind::boolean:
      return "boolean";
    case kind::string:
      return "string";
    case kind::array:
      return "array";
    default:
      return "object";
  }
}

[[nodiscard]] inline bool is_number(const value& v) noexcept
{
  return kind_of(v) == kind::number;
}

//! Numeric value of a number, whichever alternative holds it.
[[nodiscard]] inline double as_number(const value& v) noexcept
{
  if(auto i = get_if<int64_t>(&v.v))
    return double(*i);
  if(auto d = get_if<double>(&v.v))
    return *d;
  return 0.;
}

//! Build a number, keeping it integral when it exactly is one. Only affects
//! how it prints - jq holds every number as a double and prints 2, not 2.0.
[[nodiscard]] inline value number(double d) noexcept
{
  if(std::isfinite(d) && d == std::floor(d) && std::abs(d) < 9.2e18)
    return value{int64_t(d)};
  return value{d};
}

//! Only null and false are false. Zero, the empty string and the empty array
//! are all true, which is the opposite of most languages and the single most
//! common surprise in jq.
[[nodiscard]] inline bool truthy(const value& v) noexcept
{
  if(get_if<null_t>(&v.v))
    return false;
  if(auto b = get_if<bool>(&v.v))
    return *b;
  return true;
}

//! How deep a value may nest before comparing or merging it is refused.
//!
//! These walk the structure recursively, and the structure comes from outside
//! - a websocket reply, an OSC bundle. This runs on the execution thread, so
//! running out of stack is not an acceptable outcome for badly shaped input;
//! a refusal is, because the host turns it into "no output".
inline constexpr int max_depth = 128;

//! jq's total order over every pair of values: -1, 0 or 1. Numbers compare
//! numerically across the int/double split, arrays lexicographically, objects
//! by sorted keys then by the values at those keys.
[[nodiscard]] inline int compare(const value& a, const value& b, int depth = 0)
{
  if(depth > max_depth)
    throw error{"comparison is too deeply nested"};

  const auto ka = kind_of(a), kb = kind_of(b);
  if(ka != kb)
    return int(ka) < int(kb) ? -1 : 1;

  switch(ka)
  {
    case kind::null:
      return 0;
    case kind::boolean: {
      const bool x = *get_if<bool>(&a.v), y = *get_if<bool>(&b.v);
      return x == y ? 0 : (!x ? -1 : 1);
    }
    case kind::number: {
      const double x = as_number(a), y = as_number(b);
      return x < y ? -1 : (x > y ? 1 : 0);
    }
    case kind::string: {
      const auto& x = *get_if<string_type>(&a.v);
      const auto& y = *get_if<string_type>(&b.v);
      return x < y ? -1 : (x > y ? 1 : 0);
    }
    case kind::array: {
      const auto& x = *get_if<list_type>(&a.v);
      const auto& y = *get_if<list_type>(&b.v);
      const auto n = std::min(x.size(), y.size());
      for(std::size_t i = 0; i < n; i++)
        if(const int c = compare(x[i], y[i], depth + 1); c != 0)
          return c;
      return x.size() == y.size() ? 0 : (x.size() < y.size() ? -1 : 1);
    }
    default: {
      // Keys first, then the values in key order.
      const auto& x = *get_if<map_type>(&a.v);
      const auto& y = *get_if<map_type>(&b.v);
      auto ix = x.begin();
      auto iy = y.begin();
      for(; ix != x.end() && iy != y.end(); ++ix, ++iy)
        if(ix->first != iy->first)
          return ix->first < iy->first ? -1 : 1;
      if(x.size() != y.size())
        return x.size() < y.size() ? -1 : 1;

      for(ix = x.begin(), iy = y.begin(); ix != x.end(); ++ix, ++iy)
        if(const int c = compare(ix->second, iy->second, depth + 1); c != 0)
          return c;
      return 0;
    }
  }
}

[[nodiscard]] inline bool equal(const value& a, const value& b)
{
  return compare(a, b) == 0;
}

//! Rendered form used in error messages, deliberately short.
[[nodiscard]] inline std::string brief(const value& v)
{
  switch(kind_of(v))
  {
    case kind::null:
      return "null";
    case kind::boolean:
      return *get_if<bool>(&v.v) ? "true" : "false";
    case kind::number: {
      const double d = as_number(v);
      if(d == std::floor(d) && std::abs(d) < 9.2e18)
        return std::to_string(int64_t(d));
      return std::to_string(d);
    }
    case kind::string:
      return "\"" + *get_if<string_type>(&v.v) + "\"";
    case kind::array:
      return "array";
    default:
      return "object";
  }
}

[[noreturn]] inline void
type_error(const char* what, const value& a, const value& b)
{
  throw error{
      std::string{type_name(a)} + " (" + brief(a) + ") and " + type_name(b) + " ("
      + brief(b) + ") cannot be " + what};
}

/**
 * @brief jq's `+`.
 *
 * Overloaded per type: null is the identity, numbers add, strings and arrays
 * concatenate, objects merge with the right-hand side winning.
 */
[[nodiscard]] inline value add(const value& a, const value& b)
{
  if(get_if<null_t>(&a.v))
    return b;
  if(get_if<null_t>(&b.v))
    return a;

  const auto ka = kind_of(a), kb = kind_of(b);
  if(ka != kb)
    type_error("added", a, b);

  switch(ka)
  {
    case kind::number:
      return number(as_number(a) + as_number(b));
    case kind::string:
      return value{*get_if<string_type>(&a.v) + *get_if<string_type>(&b.v)};
    case kind::array: {
      list_type r = *get_if<list_type>(&a.v);
      const auto& y = *get_if<list_type>(&b.v);
      r.insert(r.end(), y.begin(), y.end());
      return value{std::move(r)};
    }
    case kind::object: {
      map_type r = *get_if<map_type>(&a.v);
      for(const auto& [k, v] : *get_if<map_type>(&b.v))
        r[k] = v;
      return value{std::move(r)};
    }
    default:
      type_error("added", a, b);
  }
}

//! jq's `-`: numbers subtract, and an array minus an array removes every
//! element of the right from the left.
[[nodiscard]] inline value subtract(const value& a, const value& b)
{
  const auto ka = kind_of(a), kb = kind_of(b);
  if(ka == kind::number && kb == kind::number)
    return number(as_number(a) - as_number(b));

  if(ka == kind::array && kb == kind::array)
  {
    const auto& y = *get_if<list_type>(&b.v);
    list_type r;
    for(const auto& e : *get_if<list_type>(&a.v))
    {
      bool drop = false;
      for(const auto& d : y)
        if(equal(e, d))
        {
          drop = true;
          break;
        }
      if(!drop)
        r.push_back(e);
    }
    return value{std::move(r)};
  }
  type_error("subtracted", a, b);
}

[[nodiscard]] inline value multiply(const value& a, const value& b, int depth = 0)
{
  const auto ka = kind_of(a), kb = kind_of(b);
  if(ka == kind::number && kb == kind::number)
    return number(as_number(a) * as_number(b));

  // Repeating a string works with the operands either way round. A negative
  // count - or a NaN, which fails every comparison - gives null; a fractional
  // one is truncated, so 1.5 * "abc" is "abc" and 0 * "abc" is "".
  {
    const value* str = nullptr;
    const value* cnt = nullptr;
    if(ka == kind::string && kb == kind::number)
    {
      str = &a;
      cnt = &b;
    }
    else if(ka == kind::number && kb == kind::string)
    {
      str = &b;
      cnt = &a;
    }

    if(str)
    {
      const double n = as_number(*cnt);
      if(!(n >= 0))
        return value{null_t{}};
      string_type r;
      const auto& s = *get_if<string_type>(&str->v);
      for(int i = 0; i < int(n); i++)
        r += s;
      return value{std::move(r)};
    }
  }
  if(ka == kind::object && kb == kind::object)
  {
    if(depth > max_depth)
      throw error{"merge is too deeply nested"};
    // Recursive merge, unlike +, which replaces.
    map_type r = *get_if<map_type>(&a.v);
    for(const auto& [k, v] : *get_if<map_type>(&b.v))
    {
      auto it = r.find(k);
      if(it != r.end() && kind_of(it->second) == kind::object
         && kind_of(v) == kind::object)
        it->second = multiply(it->second, v, depth + 1);
      else
        r[k] = v;
    }
    return value{std::move(r)};
  }
  type_error("multiplied", a, b);
}

//! jq's `/`: numbers divide, and a string divided by a string splits on it.
[[nodiscard]] inline value divide(const value& a, const value& b)
{
  const auto ka = kind_of(a), kb = kind_of(b);
  if(ka == kind::number && kb == kind::number)
  {
    const double y = as_number(b);
    if(y == 0)
      throw error{
          std::string{type_name(a)} + " (" + brief(a) + ") and " + type_name(b)
          + " (" + brief(b) + ") cannot be divided because the divisor is zero"};
    return number(as_number(a) / y);
  }
  if(ka == kind::string && kb == kind::string)
  {
    const auto& s = *get_if<string_type>(&a.v);
    const auto& sep = *get_if<string_type>(&b.v);
    list_type r;
    if(sep.empty())
    {
      for(char c : s)
        r.push_back(value{string_type(1, c)});
      return value{std::move(r)};
    }
    std::size_t pos = 0;
    while(true)
    {
      const auto next = s.find(sep, pos);
      if(next == string_type::npos)
      {
        r.push_back(value{s.substr(pos)});
        break;
      }
      r.push_back(value{s.substr(pos, next - pos)});
      pos = next + sep.size();
    }
    return value{std::move(r)};
  }
  type_error("divided", a, b);
}

//! jq's `%`: integer remainder, operands truncated towards zero.
[[nodiscard]] inline value modulo(const value& a, const value& b)
{
  if(!is_number(a) || !is_number(b))
    type_error("divided", a, b);
  const auto y = int64_t(as_number(b));
  if(y == 0)
    throw error{"cannot be divided because the divisor is zero"};
  return value{int64_t(int64_t(as_number(a)) % y)};
}

}
