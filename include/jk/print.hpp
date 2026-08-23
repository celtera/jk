#pragma once
#include <jk/ops.hpp>
#include <jk/value.hpp>

#include <jk/charconv.hpp>

#include <cmath>
#include <cstdio>
#include <iostream>
#include <sstream>
#include <stdexcept>

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
  void operator()(null_t) { os << "null"; }
  void operator()(bool t) { os << (t ? "true" : "false"); }
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
    os << "{";
    std::size_t k = 0;
    for (auto& e : t)
    {
      os << e.first << ": ";
      config::variant_ns::visit(*this, e.second.v);
      if (++k < t.size())
        os << ", ";
    }
    os << "}";
  }
};

inline std::string to_string(const jk::value& v)
{
  std::stringstream str;
  config::variant_ns::visit(print{str}, v.v);
  return str.str();
}

/**
 * @brief Render as compact JSON, matching `jq -c`.
 *
 * Separate from `print` above, which is a debugging form. This one is the
 * wire format: it is what `tostring` produces, and what the conformance tests
 * compare against jq's own output.
 *
 * Object keys come out sorted because map_type is an ordered map; jq preserves
 * insertion order unless asked otherwise, so the tests compare against
 * `jq -cS`.
 */
inline void render_number(double d, std::string& out)
{
  if(std::isnan(d))
  {
    out += "null"; // jq renders NaN as null
    return;
  }
  if(std::isinf(d))
  {
    out += d > 0 ? "1.7976931348623157e+308" : "-1.7976931348623157e+308";
    return;
  }
  if(d == std::floor(d) && std::abs(d) < 1e17)
  {
    char buf[32];
    const auto n = std::snprintf(buf, sizeof(buf), "%lld", (long long)d);
    out.append(buf, n);
    return;
  }
  // Shortest representation that round-trips, as jq emits.
  append_shortest(d, out);
}

inline void render_string(const std::string& s, std::string& out)
{
  out += '"';
  for(unsigned char c : s)
  {
    switch(c)
    {
      case '"':
        out += "\\\"";
        break;
      case '\\':
        out += "\\\\";
        break;
      case '\n':
        out += "\\n";
        break;
      case '\r':
        out += "\\r";
        break;
      case '\t':
        out += "\\t";
        break;
      case '\b':
        out += "\\b";
        break;
      case '\f':
        out += "\\f";
        break;
      default:
        if(c < 0x20)
        {
          char buf[8];
          const auto n = std::snprintf(buf, sizeof(buf), "\\u%04x", c);
          out.append(buf, n);
        }
        else
        {
          out += char(c);
        }
        break;
    }
  }
  out += '"';
}

//!  depth Guards the recursion. Anything this deep cannot have been
//! copied into jk in the first place - the value type copies recursively - so
//! refusing is strictly safer than the alternative of running out of stack.
inline void render_json(const value& v, std::string& out, int depth = 0)
{
  if(depth > 512)
    throw error{"value is too deeply nested to render"};

  if(get_if<null_t>(&v.v))
  {
    out += "null";
  }
  else if(auto b = get_if<bool>(&v.v))
  {
    out += *b ? "true" : "false";
  }
  else if(auto i = get_if<int64_t>(&v.v))
  {
    char buf[32];
    const auto n = std::snprintf(buf, sizeof(buf), "%lld", (long long)*i);
    out.append(buf, n);
  }
  else if(auto d = get_if<double>(&v.v))
  {
    render_number(*d, out);
  }
  else if(auto s = get_if<string_type>(&v.v))
  {
    render_string(*s, out);
  }
  else if(auto l = get_if<list_type>(&v.v))
  {
    out += '[';
    bool first = true;
    for(const auto& e : *l)
    {
      if(!first)
        out += ',';
      first = false;
      render_json(e, out, depth + 1);
    }
    out += ']';
  }
  else if(auto m = get_if<map_type>(&v.v))
  {
    out += '{';
    bool first = true;
    for(const auto& [k, e] : *m)
    {
      if(!first)
        out += ',';
      first = false;
      render_string(k, out);
      out += ':';
      render_json(e, out, depth + 1);
    }
    out += '}';
  }
}

inline std::string to_json(const value& v)
{
  std::string out;
  render_json(v, out);
  return out;
}
}
