#pragma once
#include <jk/generator.hpp>
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
    for (auto res : act(in))
    {
      DEBUG(" ------ * yielding: {}\n", to_string(res.data));
      co_yield res.data;
    }
  }
}
inline generator<value> copy_all(const value& in)
{
  DEBUG(" ---- * input: {}\n", to_string(in));
  DEBUG(" ------ * yielding: {}\n", to_string(in));
  co_yield in;
}

inline generator<value> access_array(int index, const value& in)
{
  DEBUG(" ---- * input: {}\n", to_string(in));
  if (auto ptr = get_if<list_type>(&in.v))
    if (index >= 0 && index < std::ssize(*ptr))
      co_yield (*ptr)[index];
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

inline generator<value>
access_array_indices(const std::vector<int>& indices, const value& in)
{
  DEBUG(" ---- * input: {}\n", to_string(in));
  if (auto ptr = get_if<list_type>(&in.v))
    for (auto& index : indices)
      if (index >= 0 && index < std::ssize(*ptr))
      {
        DEBUG(" ------ * yielding: {}\n", to_string((*ptr)[index]));
        co_yield (*ptr)[index];
      }
}

inline generator<value>
access_array_range(std::pair<int, int> indices, const value& in)
{
  DEBUG(" ---- * input: {}\n", to_string(in));
  if (auto ptr = get_if<list_type>(&in.v))
  {
    auto [a, b] = indices;
    if (a == b)
    {
      DEBUG(" ------ * yielding: []\n");

      co_yield list_type{};
    }
    else
    {
      if (a >= 0 && b >= 0)
      {
        list_type t;
        if (a < b)
        {
          for (int index = a; index < b; ++index)
          {
            if (index < std::ssize(*ptr))
            {
              t.push_back((*ptr)[index]);
            }
          }
        }
        else
        {
          for (int index = b; index > a; --index)
          {
            if (index < std::ssize(*ptr))
            {
              t.push_back((*ptr)[index]);
            }
          }
        }

        DEBUG(" ------ * yielding: {}\n", to_string(t));
        co_yield value{std::move(t)};
      }
    }
  }
}

inline generator<value>
access_member(const std::string& index, const value& in)
{
  DEBUG(" ---- * input: {}\n", to_string(in));
  if (auto ptr = get_if<map_type>(&in.v))
    if (auto it = ptr->find(index); it != ptr->end())
    {
      DEBUG(" ------ * yielding: {}\n", to_string(it->second));
      co_yield it->second;
    }
}

inline generator<value> iterate_array(const value& in)
{
  DEBUG(" ---- * input: {}\n", to_string(in));
  if (auto cur_list = get_if<std::vector<value>>(&in.v))
  {
    for (auto& e : *cur_list)
    {
      DEBUG(" ------ * yielding: {}\n", to_string(e));
      co_yield e;
    }
  }
  else if (auto cur_map = get_if<map_type>(&in.v))
  {
    for (auto& [k, v] : *cur_map)
    {
      DEBUG(" ------ * yielding: {}\n", to_string(v));
      co_yield v;
    }
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
        res.push_back(result.data);

    temps.clear();
    temps.swap(res);
  }

  for (auto& v : temps)
  {
    DEBUG(" ------ * yielding: {}\n", to_string(v));
    co_yield v;
  }
}

inline generator<value>
as_array(const value& in, const std::vector<action_fun>& acts)
{
  DEBUG(" ---- * input: {}\n", to_string(in));
  list_type t;
  for (auto& v : acts)
    for (auto res : v(in))
      t.emplace_back(std::move(res.data));

  DEBUG(" ------ * yielding: {}\n", to_string(value{t}));
  co_yield value{std::move(t)};
}

inline generator<value> as_object(
    const value& in,
    const std::vector<std::pair<std::string, action_fun>>& acts)
{
  DEBUG(" ---- * input: {}\n", to_string(in));
  map_type t;
  for (auto& [k, v] : acts)
    for (auto res : v(in))
      t.emplace(k, std::move(res.data));

  DEBUG(" ------ * yielding: {}\n", to_string(value{t}));
  co_yield value{std::move(t)};
}
}
