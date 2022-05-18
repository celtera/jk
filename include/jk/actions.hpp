#pragma once
#include <jk/value.hpp>
#include <jk/generator.hpp>
#include <functional>

namespace jk
{
namespace action
{
inline
generator<value> copy_all(const value& in)
{
  co_yield in;
}

inline
generator<value> access_array(int index, const value& in)
{
  if(auto ptr = get_if<list_type>(&in.v))
    if(index < std::ssize(*ptr))
      co_yield (*ptr)[index];
}

inline
generator<value> access_array_indices(const std::vector<int>& indices, const value& in)
{
  if(auto ptr = get_if<list_type>(&in.v))
    for(auto& index : indices)
      if(index >= 0 && index < std::ssize(*ptr))
        co_yield (*ptr)[index];
}

inline
generator<value> access_array_range(const std::pair<int, int>& indices, const value& in)
{
  if(auto ptr = get_if<list_type>(&in.v))
  {
    auto [a, b] = indices;
    if(a == b)
    {
      if(a >= 0 && a < std::ssize(*ptr)) {
        co_yield (*ptr)[a];
      }
    }
    else
    {
      if(a >= 0 && b >= 0)
      {
        if(a < b)
        {
          for(int index = a; index < b; ++index) {
            if(index < std::ssize(*ptr)) {
              co_yield (*ptr)[index];
            }
          }
        }
        else
        {
          for(int index = b; index > a; --index) {
            if(index < std::ssize(*ptr)) {
              co_yield (*ptr)[index];
            }
          }
        }
      }
    }
  }
}

inline
generator<value> access_member(const std::string& index, const value& in)
{
  if(auto ptr = get_if<map_type>(&in.v))
    if(auto it = ptr->find(index); it != ptr->end())
      co_yield it->second;
}

inline
generator<value> iterate_array(const value& in)
{
  if(auto ptr = get_if<std::vector<value>>(&in.v))
    for(auto& e : *ptr)
      co_yield e;
}

inline
generator<value> collapse(const value& in, const std::vector<action_fun>& acts)
{
  std::vector<value> temps = { in };
  std::vector<value> res;

  for(auto& act : acts)
  {
    for(auto& input : temps)
      for(auto& result : act(input))
        res.push_back(result.data);

    temps.clear();
    temps.swap(res);
  }

  for(auto& v : temps)
    co_yield v;
}

inline
generator<value> as_array(const value& in, const std::vector<action_fun>& acts)
{
  list_type t;
  for(auto& v : acts)
    for(auto res : v(in))
      t.emplace_back(std::move(res.data));

  co_yield value{std::move(t)};
}
}
}
