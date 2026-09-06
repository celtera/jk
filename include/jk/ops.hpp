#pragma once
#include <cmath>
#include <jk/value.hpp>

#include <algorithm>
#include <limits>
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

//! Runtime failures unwind the filter; only `?` and `try` suppress them.
//! Keep the jq payload distinct from the diagnostic exposed to C++ hosts.
struct error : std::runtime_error
{
  value payload;

  error(std::string_view text)
      : std::runtime_error{std::string{text}}
      , payload{persistent_text(text)}
  {
  }

  template <typename Traits, typename Allocator>
  error(const std::basic_string<char, Traits, Allocator>& text)
      : error{std::string_view{text.data(), text.size()}}
  {
  }
  error(const char* text)
      : error{std::string_view{text}}
  {
  }

  explicit error(const value& data)
      : std::runtime_error{message(data)}
      , payload{persistent_copy(data)}
  {
  }

  error(const error& other)
      : std::runtime_error{other}
      , payload{persistent_copy(other.payload)}
  {
  }

  error(error&&) noexcept = default;
  error& operator=(const error& other)
  {
    if (this != &other)
      *this = error{other};
    return *this;
  }
  error& operator=(error&&) noexcept = default;

private:
  // An exception can outlive the evaluation scope it unwinds through.
  static value persistent_copy(const value& data)
  {
    allocation_scope heap{nullptr};
    return data;
  }

  static value persistent_text(std::string_view text)
  {
    allocation_scope heap{nullptr};
    return value{text};
  }

  static std::string message(const value& data)
  {
    if (const auto* s = get_if<string_type>(&data.v))
      return std::string{s->data(), s->size()};
    return "jq error";
  }
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
  if (get_if<null_t>(&v.v))
    return kind::null;
  if (get_if<int64_t>(&v.v) || get_if<double>(&v.v))
    return kind::number;
  if (get_if<bool>(&v.v))
    return kind::boolean;
  if (get_if<string_type>(&v.v))
    return kind::string;
  if (get_if<list_type>(&v.v))
    return kind::array;
  return kind::object;
}

[[nodiscard]] inline const char* type_name(const value& v) noexcept
{
  switch (kind_of(v))
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
  if (auto i = get_if<int64_t>(&v.v))
    return double(*i);
  if (auto d = get_if<double>(&v.v))
    return *d;
  return 0.;
}

//! Preserve exact integral results within int64's range. Arithmetic still
//! computes in binary64; this does not preserve arbitrary decimal literals.
[[nodiscard]] inline value number(double d) noexcept
{
  if (d >= -0x1p63 && d < 0x1p63 && d == std::trunc(d))
    return value{static_cast<int64_t>(d)};
  return value{d};
}

//! Only null and false are false. Zero, the empty string and the empty array
//! are all true, which is the opposite of most languages and the single most
//! common surprise in jq.
[[nodiscard]] inline bool truthy(const value& v) noexcept
{
  if (get_if<null_t>(&v.v))
    return false;
  if (auto b = get_if<bool>(&v.v))
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

//! Compare without rounding an int64 operand through binary64 first.
//! The range checks also exclude nonfinite values before the integer cast.
[[nodiscard]] inline int compare_integer_double(int64_t i, double d) noexcept
{
  if (std::isnan(d) || d < -0x1p63)
    return 1;
  if (d >= 0x1p63)
    return -1;
  const auto truncated = static_cast<int64_t>(d);
  if (i != truncated)
    return i < truncated ? -1 : 1;
  const double integral = static_cast<double>(truncated);
  return integral < d ? -1 : (integral > d ? 1 : 0);
}

//! Sorting treats NaNs as equivalent and before all other numbers, while
//! equality below deliberately retains IEEE's NaN != NaN behavior.
[[nodiscard]] inline int
compare_numbers(const value& a, const value& b) noexcept
{
  if (const auto* x = get_if<int64_t>(&a.v))
  {
    if (const auto* y = get_if<int64_t>(&b.v))
      return *x < *y ? -1 : (*x > *y ? 1 : 0);
    return compare_integer_double(*x, *get_if<double>(&b.v));
  }
  const double x = *get_if<double>(&a.v);
  if (const auto* y = get_if<int64_t>(&b.v))
    return -compare_integer_double(*y, x);
  const double y = *get_if<double>(&b.v);
  if (std::isnan(x))
    return std::isnan(y) ? 0 : -1;
  if (std::isnan(y))
    return 1;
  return x < y ? -1 : (x > y ? 1 : 0);
}

//! jq's total order over every pair of values: -1, 0 or 1. Numbers compare
//! numerically across the int/double split, arrays lexicographically, objects
//! by sorted keys then by the values at those keys.
[[nodiscard]] inline int compare(const value& a, const value& b, int depth = 0)
{
  if (depth > max_depth)
    throw error{"comparison is too deeply nested"};

  const auto ka = kind_of(a), kb = kind_of(b);
  if (ka != kb)
    return int(ka) < int(kb) ? -1 : 1;

  switch (ka)
  {
    case kind::null:
      return 0;
    case kind::boolean:
    {
      const bool x = *get_if<bool>(&a.v), y = *get_if<bool>(&b.v);
      return x == y ? 0 : (!x ? -1 : 1);
    }
    case kind::number:
      return compare_numbers(a, b);
    case kind::string:
    {
      const auto& x = *get_if<string_type>(&a.v);
      const auto& y = *get_if<string_type>(&b.v);
      return x < y ? -1 : (x > y ? 1 : 0);
    }
    case kind::array:
    {
      const auto& x = *get_if<list_type>(&a.v);
      const auto& y = *get_if<list_type>(&b.v);
      const auto n = std::min(x.size(), y.size());
      for (std::size_t i = 0; i < n; i++)
        if (const int c = compare(x[i], y[i], depth + 1); c != 0)
          return c;
      return x.size() == y.size() ? 0 : (x.size() < y.size() ? -1 : 1);
    }
    default:
    {
      // Keys first, then the values in key order.
      const auto& x = *get_if<map_type>(&a.v);
      const auto& y = *get_if<map_type>(&b.v);
      auto ix = x.begin();
      auto iy = y.begin();
      for (; ix != x.end() && iy != y.end(); ++ix, ++iy)
        if (ix->first != iy->first)
          return ix->first < iy->first ? -1 : 1;
      if (x.size() != y.size())
        return x.size() < y.size() ? -1 : 1;

      for (ix = x.begin(), iy = y.begin(); ix != x.end(); ++ix, ++iy)
        if (const int c = compare(ix->second, iy->second, depth + 1); c != 0)
          return c;
      return 0;
    }
  }
}

//! Structural equality is not sort equivalence: a NaN at any depth is unequal.
[[nodiscard]] inline bool equal(const value& a, const value& b, int depth = 0)
{
  if (depth > max_depth)
    throw error{"equality is too deeply nested"};
  const auto k = kind_of(a);
  if (k != kind_of(b))
    return false;
  switch (k)
  {
    case kind::null:
      return true;
    case kind::boolean:
      return *get_if<bool>(&a.v) == *get_if<bool>(&b.v);
    case kind::number:
      if (const auto* x = get_if<double>(&a.v); x && std::isnan(*x))
        return false;
      if (const auto* y = get_if<double>(&b.v); y && std::isnan(*y))
        return false;
      return compare_numbers(a, b) == 0;
    case kind::string:
      return *get_if<string_type>(&a.v) == *get_if<string_type>(&b.v);
    case kind::array:
    {
      const auto& x = *get_if<list_type>(&a.v);
      const auto& y = *get_if<list_type>(&b.v);
      if (x.size() != y.size())
        return false;
      for (std::size_t i = 0; i < x.size(); ++i)
        if (!equal(x[i], y[i], depth + 1))
          return false;
      return true;
    }
    default:
    {
      const auto& x = *get_if<map_type>(&a.v);
      const auto& y = *get_if<map_type>(&b.v);
      if (x.size() != y.size())
        return false;
      auto iy = y.begin();
      for (auto ix = x.begin(); ix != x.end(); ++ix, ++iy)
        if (ix->first != iy->first
            || !equal(ix->second, iy->second, depth + 1))
          return false;
      return true;
    }
  }
}

//! Rendered form used in error messages, deliberately short.
[[nodiscard]] inline std::string brief(const value& v)
{
  switch (kind_of(v))
  {
    case kind::null:
      return "null";
    case kind::boolean:
      return *get_if<bool>(&v.v) ? "true" : "false";
    case kind::number:
    {
      if (const auto* i = get_if<int64_t>(&v.v))
        return std::to_string(*i);
      return std::to_string(*get_if<double>(&v.v));
    }
    case kind::string:
    {
      const auto& s = *get_if<string_type>(&v.v);
      return "\"" + std::string{s.data(), s.size()} + "\"";
    }
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
      std::string{type_name(a)} + " (" + brief(a) + ") and " + type_name(b)
      + " (" + brief(b) + ") cannot be " + what};
}

/**
 * @brief jq's `+`.
 *
 * Overloaded per type: null is the identity, numbers add, strings and arrays
 * concatenate, objects merge with the right-hand side winning.
 */
[[nodiscard]] inline value add(const value& a, const value& b)
{
  if (get_if<null_t>(&a.v))
    return b;
  if (get_if<null_t>(&b.v))
    return a;

  const auto ka = kind_of(a), kb = kind_of(b);
  if (ka != kb)
    type_error("added", a, b);

  switch (ka)
  {
    case kind::number:
      return number(as_number(a) + as_number(b));
    case kind::string:
      return value{*get_if<string_type>(&a.v) + *get_if<string_type>(&b.v)};
    case kind::array:
    {
      list_type r = *get_if<list_type>(&a.v);
      const auto& y = *get_if<list_type>(&b.v);
      r.insert(r.end(), y.begin(), y.end());
      return value{std::move(r)};
    }
    case kind::object:
    {
      map_type r = *get_if<map_type>(&a.v);
      for (const auto& [k, v] : *get_if<map_type>(&b.v))
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
  if (ka == kind::number && kb == kind::number)
    return number(as_number(a) - as_number(b));

  if (ka == kind::array && kb == kind::array)
  {
    const auto& y = *get_if<list_type>(&b.v);
    list_type r;
    for (const auto& e : *get_if<list_type>(&a.v))
    {
      bool drop = false;
      for (const auto& d : y)
        if (equal(e, d))
        {
          drop = true;
          break;
        }
      if (!drop)
        r.push_back(e);
    }
    return value{std::move(r)};
  }
  type_error("subtracted", a, b);
}

[[nodiscard]] inline value
multiply(const value& a, const value& b, int depth = 0)
{
  const auto ka = kind_of(a), kb = kind_of(b);
  if (ka == kind::number && kb == kind::number)
    return number(as_number(a) * as_number(b));

  // Repeating accepts either operand order. Negative and NaN counts produce
  // null; other counts truncate and saturate at jq's maximum string count.
  {
    const value* str = nullptr;
    const value* cnt = nullptr;
    if (ka == kind::string && kb == kind::number)
    {
      str = &a;
      cnt = &b;
    }
    else if (ka == kind::number && kb == kind::string)
    {
      str = &b;
      cnt = &a;
    }

    if (str)
    {
      const double n = as_number(*cnt);
      if (!(n >= 0))
        return value{null_t{}};
      const auto& s = *get_if<string_type>(&str->v);
      string_type r;
      if (s.empty() || n < 1)
        return value{std::move(r)};
      constexpr auto max_count = std::numeric_limits<int>::max();
      const auto count = n >= max_count ? std::size_t(max_count)
                                        : static_cast<std::size_t>(n);
      const auto limit = std::min(r.max_size(), std::size_t(max_count) - 1);
      if (count > limit / s.size())
        throw error{"Repeat string result too long"};
      const auto size = s.size() * count;
      r.reserve(size);
      r += s;
      while (r.size() < size)
        r.append(r.data(), std::min(r.size(), size - r.size()));
      return value{std::move(r)};
    }
  }
  if (ka == kind::object && kb == kind::object)
  {
    if (depth > max_depth)
      throw error{"merge is too deeply nested"};
    // Recursive merge, unlike +, which replaces.
    map_type r = *get_if<map_type>(&a.v);
    for (const auto& [k, v] : *get_if<map_type>(&b.v))
    {
      auto it = r.find(k);
      if (it != r.end() && kind_of(it->second) == kind::object
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
  if (ka == kind::number && kb == kind::number)
  {
    const double y = as_number(b);
    if (y == 0)
      throw error{
          std::string{type_name(a)} + " (" + brief(a) + ") and " + type_name(b)
          + " (" + brief(b)
          + ") cannot be divided because the divisor is zero"};
    return number(as_number(a) / y);
  }
  if (ka == kind::string && kb == kind::string)
  {
    const auto& s = *get_if<string_type>(&a.v);
    const auto& sep = *get_if<string_type>(&b.v);
    list_type r;
    if (sep.empty())
    {
      for (std::size_t pos = 0; pos < s.size();)
      {
        const auto lead = static_cast<unsigned char>(s[pos]);
        const std::size_t width = lead >= 0xc2 && lead <= 0xdf   ? 2
                                  : lead >= 0xe0 && lead <= 0xef ? 3
                                  : lead >= 0xf0 && lead <= 0xf4 ? 4
                                                                 : 1;
        std::size_t end = pos + 1;
        while (end < s.size() && end - pos < width
               && (static_cast<unsigned char>(s[end]) & 0xc0) == 0x80)
          ++end;
        r.push_back(value{s.substr(pos, end - pos)});
        pos = end;
      }
      return value{std::move(r)};
    }
    std::size_t pos = 0;
    while (true)
    {
      const auto next = s.find(sep, pos);
      if (next == string_type::npos)
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

//! jq truncates remainder operands and saturates at int64 limits. Callers
//! handle NaN first, so every cast here is finite and representable.
[[nodiscard]] inline int64_t remainder_integer(const value& v) noexcept
{
  if (const auto* i = get_if<int64_t>(&v.v))
    return *i;
  const double d = *get_if<double>(&v.v);
  if (d <= -0x1p63)
    return std::numeric_limits<int64_t>::min();
  if (d >= 0x1p63)
    return std::numeric_limits<int64_t>::max();
  return static_cast<int64_t>(d);
}

//! jq's `%`: integer remainder, operands truncated towards zero.
[[nodiscard]] inline value modulo(const value& a, const value& b)
{
  if (!is_number(a) || !is_number(b))
    type_error("divided", a, b);
  if (const auto* x = get_if<double>(&a.v); x && std::isnan(*x))
    return value{*x};
  if (const auto* y = get_if<double>(&b.v); y && std::isnan(*y))
    return value{*y};
  const auto y = remainder_integer(b);
  if (y == 0)
    throw error{"cannot be divided (remainder) because the divisor is zero"};
  return value{y == -1 ? int64_t{0} : remainder_integer(a) % y};
}

}
