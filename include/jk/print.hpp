#pragma once
#include <jk/value.hpp>
#include <iostream>
#include <sstream>

namespace jk
{
struct print
{
  std::ostream& os;
  print(std::ostream& s = std::cerr): os{s} { }

  void operator()(const auto& t) { os << t; }
  void operator()(const std::string& t) { os << "\"" << t << "\""; }
  void operator()(const list_type& t)
  {
    os << "[";
    for (int i = 0; i < t.size(); i++)
    {
      visit(*this, t[i].v);

      if(i < t.size() - 1)
        os << ", ";
    }
    os << "]";
  }
  void operator()(const map_type& t)
  {
    os << "[";
    int k = 0;
    for (auto& e : t)
    {
      os << e.first << ": ";
      visit(*this, e.second.v);
      if(k++ < t.size())
        os << ", ";
    }
    os << "]";
  }
};

inline std::string to_string(const jk::value& v)
{
  std::stringstream str;
  visit(print{str}, v.v);
  return str.str();
}
}
