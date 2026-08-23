#pragma once
#include <jk/generator.hpp>
#include <jk/ops.hpp>
#include <jk/value.hpp>

#include <functional>
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
      DEBUG(" ------ * yielding: {}\n", to_string(res.data));
      co_yield std::move(res.data);
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
  for(auto& x : a(in))
    for(auto& y : b(x.data))
      co_yield std::move(y.data);
}

//! `a , b`: the outputs of a, then those of b, both from the same input.
inline generator<value>
concat(const value& in, const action_fun& a, const action_fun& b)
{
  for(auto& x : a(in))
    co_yield std::move(x.data);
  for(auto& y : b(in))
    co_yield std::move(y.data);
}

/**
 * @brief `.[n]`.
 *
 * jq indexes from the end for a negative n, and produces null rather than
 * nothing when the index is out of range - an absent element is a null, which
 * is what lets `.[5] // "default"` work.
 */
inline generator<value> access_array(int index, const value& in)
{
  if(get_if<null_t>(&in.v))
  {
    co_yield value{null_t{}};
  }
  else if(auto ptr = get_if<list_type>(&in.v))
  {
    const auto n = std::ssize(*ptr);
    const auto i = index < 0 ? n + index : index;
    if(i >= 0 && i < n)
      co_yield (*ptr)[i];
    else
      co_yield value{null_t{}};
  }
  else
  {
    throw error{
        "Cannot index " + std::string{type_name(in)} + " with number"};
  }
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

//! `.[a, b]`, which jq defines as `.[a], .[b]`.
inline generator<value>
access_array_indices(const std::vector<int>& indices, const value& in)
{
  for (auto index : indices)
    for(auto& v : access_array(index, in))
      co_yield std::move(v.data);
}

/**
 * @brief `.[a:b]`, with either end optional.
 *
 * jq clamps both ends into range, counts a negative from the end, and yields
 * an empty array when the range is inverted rather than reversing it.
 */
inline generator<value> access_array_range(
    int a, int b, bool has_a, bool has_b, const value& in)
{
  if(get_if<null_t>(&in.v))
  {
    co_yield value{null_t{}};
    co_return;
  }

  const auto clamp = [](int idx, std::ptrdiff_t n) -> std::ptrdiff_t {
    std::ptrdiff_t i = idx < 0 ? n + idx : idx;
    return i < 0 ? 0 : (i > n ? n : i);
  };

  if(auto ptr = get_if<list_type>(&in.v))
  {
    const auto n = std::ssize(*ptr);
    const auto from = has_a ? clamp(a, n) : 0;
    const auto to = has_b ? clamp(b, n) : n;

    list_type t;
    for(auto i = from; i < to; ++i)
      t.push_back((*ptr)[i]);
    co_yield value{std::move(t)};
  }
  else if(auto str = get_if<string_type>(&in.v))
  {
    const auto n = std::ptrdiff_t(str->size());
    const auto from = has_a ? clamp(a, n) : 0;
    const auto to = has_b ? clamp(b, n) : n;
    co_yield value{from < to ? str->substr(from, to - from) : string_type{}};
  }
  else
  {
    throw error{"Cannot index " + std::string{type_name(in)} + " with object"};
  }
}

/**
 * @brief `.foo`.
 *
 * A missing key is null, not nothing, and null indexes as null so that
 * `.a.b.c` on a shallow object is null rather than an error.
 */
inline generator<value>
access_member(const std::string& index, const value& in)
{
  if(get_if<null_t>(&in.v))
  {
    co_yield value{null_t{}};
  }
  else if(auto ptr = get_if<map_type>(&in.v))
  {
    if (auto it = ptr->find(index); it != ptr->end())
      co_yield it->second;
    else
      co_yield value{null_t{}};
  }
  else
  {
    throw error{
        "Cannot index " + std::string{type_name(in)} + " with \"" + index + "\""};
  }
}

//! `.[expr]`: index by a computed key, string or number depending on the
//! container.
inline generator<value>
index_by(const value& in, const action_fun& base, const action_fun& key)
{
  for(auto& b : base(in))
  {
    for(auto& k : key(in))
    {
      if(auto s = get_if<string_type>(&k.data.v))
      {
        for(auto& v : access_member(*s, b.data))
          co_yield std::move(v.data);
      }
      else if(is_number(k.data))
      {
        for(auto& v : access_array(int(as_number(k.data)), b.data))
          co_yield std::move(v.data);
      }
      else
      {
        throw error{
            "Cannot index " + std::string{type_name(b.data)} + " with "
            + type_name(k.data)};
      }
    }
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
    throw error{"Cannot iterate over " + std::string{type_name(in)} + " ("
                + brief(in) + ")"};
  }
}

inline generator<value>
collapse_pipe(const value& in, const std::vector<action_fun>& acts)
{
  DEBUG(" ---- * input: {}\n", to_string(in));
  std::vector<value> temps = {in};
  std::vector<value> res;

  for (auto& act : acts)
  {
    for (auto& input : temps)
      for (auto& result : act(input))
        res.push_back(std::move(result.data));

    temps.clear();
    temps.swap(res);
  }

  for (auto& v : temps)
    co_yield v;
}

//! `[ e ]`: collect every output of e into one array.
inline generator<value> as_array(const value& in, const action_fun& act)
{
  list_type t;
  for (auto& res : act(in))
    t.emplace_back(std::move(res.data));

  co_yield value{std::move(t)};
}

/**
 * @brief `{ k: v, ... }`.
 *
 * Each key and each value is itself an expression that may produce several
 * results, and jq then builds one object per combination: `{a: (1,2)}` is two
 * objects, not one. The recursion walks the members left to right, which is
 * the order jq varies them in.
 */
inline void build_objects(
    const value& in,
    const std::vector<std::pair<action_fun, action_fun>>& members,
    std::size_t idx,
    map_type& acc,
    list_type& out)
{
  if(idx == members.size())
  {
    out.push_back(value{acc});
    return;
  }

  const auto& [kf, vf] = members[idx];
  for(auto& k : kf(in))
  {
    auto ks = get_if<string_type>(&k.data.v);
    if(!ks)
      throw error{"Object keys must be strings"};

    for(auto& v : vf(in))
    {
      auto saved = acc.find(*ks) != acc.end()
                       ? std::optional<value>{acc[*ks]}
                       : std::nullopt;
      acc[*ks] = std::move(v.data);
      build_objects(in, members, idx + 1, acc, out);
      if(saved)
        acc[*ks] = std::move(*saved);
      else
        acc.erase(*ks);
    }
  }
}

inline generator<value> as_object(
    const value& in,
    const std::vector<std::pair<action_fun, action_fun>>& members)
{
  list_type out;
  map_type acc;
  build_objects(in, members, 0, acc, out);
  for(auto& o : out)
    co_yield std::move(o);
}
}
