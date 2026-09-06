#pragma once
#include <cmath>
#include <jk/action_fun.hpp>
#include <jk/generator.hpp>
#include <jk/ops.hpp>
#include <jk/value.hpp>

#include <algorithm>
#include <functional>
#include <optional>
#include <vector>

#include <string_view>
#if defined(JK_DEBUG_ACTIONS)
#include <fmt/printf.h>
#include <jk/print.hpp>
#define DEBUG(...) fmt::print(stdout, __VA_ARGS__);
#else
#define DEBUG(...)
#endif
namespace jk::action
{
inline generator<value>
process_sequence(const value& in, const std::vector<action_fun>& acts)
{
  DEBUG(" ---- * input: {}\n", to_string(in));
  for (const auto& act : acts)
  {
    for (auto& res : act(in))
    {
      DEBUG(" ------ * yielding: {}\n", to_string(res.get()));
      co_yield res;
    }
  }
}

inline generator<value> copy_all(const value& in)
{
  co_yield in;
}

//! `a | b`: every output of a is fed to b.
inline generator<value>
compose(const value& in, const action_fun& a, const action_fun& b)
{
  for (auto& x : a(in))
    for (auto& y : b(x.get()))
      co_yield y;
}

//! `a , b`: the outputs of a, then those of b, both from the same input.
inline generator<value>
concat(const value& in, const action_fun& a, const action_fun& b)
{
  for (auto& x : a(in))
    co_yield x;
  for (auto& y : b(in))
    co_yield y;
}

/**
 * @brief `.[n]`.
 *
 * jq indexes from the end for a negative n, and produces null rather than
 * nothing when the index is out of range - an absent element is a null, which
 * is what lets `.[5] // "default"` work.
 */
inline generator<value> access_array(double index, const value& in)
{
  if (get_if<null_t>(&in.v))
    co_yield value{null_t{}};
  else if (auto ptr = get_if<list_type>(&in.v))
  {
    // jq truncates before interpreting a negative index. Test the range as a
    // double first: converting NaN, infinity or a huge index is undefined.
    double i = std::trunc(index);
    if (i < 0)
      i += ptr->size();
    if (i >= 0 && i < ptr->size())
      co_yield (*ptr)[static_cast<std::size_t>(i)];
    else
      co_yield value{null_t{}};
  }
  else
    throw error{"Cannot index " + std::string{type_name(in)} + " with number"};
}

inline generator<value> recurse(const value& in)
{
  DEBUG(" ---- * input: {}\n", to_string(in));

  config::vector<const jk::value*> st;
  st.push_back(&in);

  while (!st.empty())
  {
    const jk::value* cur_elt = st.back();
    st.pop_back();

    co_yield *cur_elt;

    const list_type* cur_list{};
    const map_type* cur_map{};

    // Then we iterate it recursively.
    // Fixme: this is veeeeery inefficient... but
    // did not find a way to get recursive coroutines work
    if ((cur_list = get_if<list_type>(&cur_elt->v)))
      for (auto it = cur_list->rbegin(); it != cur_list->rend(); ++it)
        st.push_back(&*it);
    else if ((cur_map = get_if<map_type>(&cur_elt->v)))
      for (auto it = cur_map->rbegin(); it != cur_map->rend(); ++it)
        st.push_back(&it->second);
  }
}

/**
 * @brief `.[a:b]`, with either end optional.
 *
 * jq clamps both ends into range, counts a negative from the end, and yields
 * an empty array when the range is inverted rather than reversing it.
 */
inline value
access_array_range(const value& a, const value& b, const value& in)
{
  if (get_if<null_t>(&in.v))
    return value{null_t{}};
  const auto array = get_if<list_type>(&in.v);
  const auto string = get_if<string_type>(&in.v);
  if (!array && !string)
    throw error{"Cannot index " + std::string{type_name(in)} + " with object"};
  if ((!get_if<null_t>(&a.v) && !is_number(a))
      || (!get_if<null_t>(&b.v) && !is_number(b)))
    throw error{"Array/string slice indices must be integers"};

  std::size_t size = array ? array->size() : 0;
  if (string)
    for (unsigned char c : *string)
      size += (c & 0xC0) != 0x80;

  const double n = static_cast<double>(size);
  double from = is_number(a) ? as_number(a) : 0;
  double to = is_number(b) ? as_number(b) : n;
  if (std::isnan(from))
    from = 0;
  if (std::isnan(to))
    to = n;
  if (from < 0)
    from += n;
  if (to < 0)
    to += n;
  from = std::floor(std::clamp(from, 0.0, n));
  to = std::ceil(std::clamp(to, from, n));
  const auto first = static_cast<std::size_t>(from);
  const auto last = static_cast<std::size_t>(to);
  if (array)
    return value{list_type(array->begin() + first, array->begin() + last)};

  // Slice by Unicode codepoints, not UTF-8 bytes; scan without allocating an
  // offsets table for every input string.
  std::size_t byte = 0, cp = 0;
  while (byte < string->size() && cp < first)
  {
    ++byte;
    while (byte < string->size()
           && (static_cast<unsigned char>((*string)[byte]) & 0xC0) == 0x80)
      ++byte;
    ++cp;
  }
  const auto start = byte;
  while (byte < string->size() && cp < last)
  {
    ++byte;
    while (byte < string->size()
           && (static_cast<unsigned char>((*string)[byte]) & 0xC0) == 0x80)
      ++byte;
    ++cp;
  }
  return value{string->substr(start, byte - start)};
}

/**
 * @brief `.foo`.
 *
 * A missing key is null, not nothing, and null indexes as null so that
 * `.a.b.c` on a shallow object is null rather than an error.
 */
inline generator<value> access_member(std::string_view index, const value& in)
{
  if (get_if<null_t>(&in.v))
  {
    co_yield value{null_t{}};
  }
  else if (auto ptr = get_if<map_type>(&in.v))
  {
    if (auto it = ptr->find(index); it != ptr->end())
      co_yield it->second;
    else
      co_yield value{null_t{}};
  }
  else
  {
    throw error{
        "Cannot index " + std::string{type_name(in)} + " with \""
        + std::string{index} + "\""};
  }
}

//! `.[expr]`: index by a computed key, string or number depending on the
//! container.
inline generator<value> index_by(
    const value& in,
    const action_fun& base,
    const action_fun& key,
    bool suppress_access_errors = false)
{
  // Keys and bases are evaluated outside the optional-access boundary.
  // In particular .a.b? must not suppress a failure evaluating .a.
  for (auto& k : key(in))
  {
    for (auto& b : base(in))
    {
      try
      {
        if (auto s = get_if<string_type>(&k.get().v))
        {
          for (auto& v : access_member(*s, b.get()))
            co_yield v;
        }
        else if (is_number(k.get()))
        {
          for (auto& v : access_array(as_number(k.get()), b.get()))
            co_yield v;
        }
        else if (auto slice = get_if<map_type>(&k.get().v))
        {
          const value missing{null_t{}};
          auto a = slice->find("start");
          auto z = slice->find("end");
          if ((get_if<list_type>(&b.get().v)
               || get_if<string_type>(&b.get().v))
              && (a == slice->end() || z == slice->end()))
            throw error{"Array/string slice indices must be integers"};
          co_yield access_array_range(
              a == slice->end() ? missing : a->second,
              z == slice->end() ? missing : z->second,
              b.get());
        }
        else
          throw error{
              "Cannot index " + std::string{type_name(b.get())} + " with "
              + type_name(k.get())};
      }
      catch (const error&)
      {
        if (!suppress_access_errors)
          throw;
      }
    }
  }
}

//! The two bounds are filters on the original input, just like computed keys.
inline generator<value>
slice_keys(const value& in, const action_fun& start, const action_fun& end)
{
  for (auto& a : start(in))
    for (auto& b : end(in))
    {
      map_type key;
      key.emplace("start", a.get());
      key.emplace("end", b.take());
      co_yield value{std::move(key)};
    }
}

inline generator<value> iterate_array(const value& in)
{
  if (auto cur_list = get_if<list_type>(&in.v))
  {
    for (auto& e : *cur_list)
      co_yield e;
  }
  else if (auto cur_map = get_if<map_type>(&in.v))
  {
    for (auto& [k, v] : *cur_map)
      co_yield v;
  }
  else
  {
    throw error{
        "Cannot iterate over " + std::string{type_name(in)} + " (" + brief(in)
        + ")"};
  }
}

//! `[ e ]`: collect every output of e into one array.
inline generator<value> as_array(const value& in, const action_fun& act)
{
  list_type t;
  for (auto& res : act(in))
    t.emplace_back(res.take());

  co_yield value{std::move(t)};
}

struct object_member
{
  action_fun key;
  action_fun filter;
  bool shorthand{};
};

/**
 * @brief `{ k: v, ... }`.
 *
 * Each key and each value is itself an expression that may produce several
 * results, and jq then builds one object per combination: `{a: (1,2)}` is two
 * objects, not one. The recursion walks the members left to right, which is
 * the order jq varies them in.
 */
inline generator<value> build_objects(
    const value& in,
    const std::vector<object_member>& members,
    std::size_t idx,
    map_type& acc)
{
  if (idx == members.size())
  {
    co_yield value{acc};
    co_return;
  }

  const auto& member = members[idx];
  for (auto& k : member.key(in))
  {
    auto ks = get_if<string_type>(&k.get().v);
    if (!ks)
      throw error{"Object keys must be strings"};

    // A shorthand lookup is correlated with this key, not another execution
    // of the key filter (which may itself emit several interpolated strings).
    auto values
        = member.shorthand ? access_member(*ks, in) : member.filter(in);
    for (auto& v : values)
    {
      const auto existing = acc.find(*ks);
      auto saved = existing != acc.end()
                       ? std::optional<value>{std::move(existing->second)}
                       : std::nullopt;
      acc[*ks] = v.take();
      for (auto& object : build_objects(in, members, idx + 1, acc))
        co_yield object;
      if (saved)
        acc[*ks] = std::move(*saved);
      else
        acc.erase(*ks);
    }
  }
}

inline generator<value>
as_object(const value& in, const std::vector<object_member>& members)
{
  map_type acc;
  for (auto& object : build_objects(in, members, 0, acc))
    co_yield object;
}
}
