#pragma once
#include <jk/value.hpp>

#include <iostream>
#include <sstream>

namespace jk
{
struct print
{
  std::ostream& os;
  print(std::ostream& s = std::cerr)
      : os{s}
  {
  }

  void operator()(const auto& t) { os << t; }
  void operator()(const std::string& t) { os << "\"" << t << "\""; }
  void operator()(const list_type& t)
  {
    os << "[";
    for (std::size_t i = 0; i < t.size(); i++)
    {
      config::variant_ns::visit(*this, t[i].v);

      if (i < t.size() - 1)
        os << ", ";
    }
    os << "]";
  }
  void operator()(const map_type& t)
  {
    os << "[";
    std::size_t k = 0;
    for (auto& e : t)
    {
      os << e.first << ": ";
      config::variant_ns::visit(*this, e.second.v);
      if (k++ < t.size())
        os << ", ";
    }
    os << "]";
  }
};

inline std::string to_string(const jk::value& v)
{
  std::stringstream str;
  config::variant_ns::visit(print{str}, v.v);
  return str.str();
}
}
