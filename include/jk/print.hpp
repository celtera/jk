#pragma once
#include <jk/value.hpp>
#include <iostream>

namespace jk
{
struct print
{
  void operator()(const auto& t) { std::cerr << t; }
  void operator()(const std::string& t) { std::cerr << "\"" << t << "\""; }
  void operator()(const list_type& t)
  {
    std::cerr << "[";
    for (int i = 0; i < t.size(); i++)
    {
      visit(*this, t[i].v);

      if(i < t.size() - 1)
        std::cerr << ", ";
    }
    std::cerr << "]";
  }
  void operator()(const map_type& t)
  {
    std::cerr << "[";
    int k = 0;
    for (auto& e : t)
    {
      std::cerr << e.first << ": ";
      visit(*this, e.second.v);
      if(k++ < t.size())
        std::cerr << ", ";
    }
    std::cerr << "]";
  }
};
}
