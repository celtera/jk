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
      if (a >= 0 && a < std::ssize(*ptr))
      {
        DEBUG(" ------ * yielding: {}\n", to_string((*ptr)[a]));
        co_yield (*ptr)[a];
      }
    }
    else
    {
      if (a >= 0 && b >= 0)
      {
        if (a < b)
        {
          for (int index = a; index < b; ++index)
          {
            if (index < std::ssize(*ptr))
            {
              DEBUG(" ------ * yielding: {}\n", to_string((*ptr)[index]));
              co_yield (*ptr)[index];
            }
          }
        }
        else
        {
          for (int index = b; index > a; --index)
          {
            if (index < std::ssize(*ptr))
            {
              DEBUG(" ------ * yielding: {}\n", to_string((*ptr)[index]));
              co_yield (*ptr)[index];
            }
          }
        }
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
  if (auto ptr = get_if<std::vector<value>>(&in.v))
  {
    for (auto& e : *ptr)
    {
      DEBUG(" ------ * yielding: {}\n", to_string(e));
      co_yield e;
    }
  }
  else if(auto ptr = get_if<map_type>(&in.v))
  {
    for (auto& [k, v] : *ptr)
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
