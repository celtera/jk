#pragma once
// A small JSON reader for the test drivers only. jk itself never parses JSON -
// it is handed values by the host - so this exists purely so the test tables
// can be written as JSON text.
#include <jk/ops.hpp>
#include <jk/value.hpp>

#include <cctype>
#include <stdexcept>
#include <string>
#include <string_view>

namespace jk_test
{
inline void append_utf8(std::string& out, unsigned cp)
{
  if(cp < 0x80)
  {
    out += char(cp);
  }
  else if(cp < 0x800)
  {
    out += char(0xC0 | (cp >> 6));
    out += char(0x80 | (cp & 0x3F));
  }
  else if(cp < 0x10000)
  {
    out += char(0xE0 | (cp >> 12));
    out += char(0x80 | ((cp >> 6) & 0x3F));
    out += char(0x80 | (cp & 0x3F));
  }
  else
  {
    out += char(0xF0 | (cp >> 18));
    out += char(0x80 | ((cp >> 12) & 0x3F));
    out += char(0x80 | ((cp >> 6) & 0x3F));
    out += char(0x80 | (cp & 0x3F));
  }
}

struct json_reader
{
  std::string_view s;
  std::size_t i = 0;

  void ws()
  {
    while(i < s.size()
          && (s[i] == ' ' || s[i] == '\n' || s[i] == '\t' || s[i] == '\r'))
      i++;
  }

  bool eat(char c)
  {
    ws();
    if(i < s.size() && s[i] == c)
    {
      i++;
      return true;
    }
    return false;
  }

  bool lit(std::string_view w)
  {
    ws();
    if(s.compare(i, w.size(), w) == 0)
    {
      i += w.size();
      return true;
    }
    return false;
  }

  unsigned hex4(std::size_t at) const
  {
    if(at + 4 > s.size())
      throw std::runtime_error{"bad \\u"};
    return (unsigned)std::stoul(std::string{s.substr(at, 4)}, nullptr, 16);
  }

  std::string str()
  {
    ws();
    std::string out;
    if(i >= s.size() || s[i] != '"')
      throw std::runtime_error{"expected string"};
    i++;
    while(i < s.size() && s[i] != '"')
    {
      if(s[i] == '\\' && i + 1 < s.size())
      {
        i++;
        switch(s[i])
        {
          case 'n': out += '\n'; break;
          case 't': out += '\t'; break;
          case 'r': out += '\r'; break;
          case 'b': out += '\b'; break;
          case 'f': out += '\f'; break;
          case 'u': {
            unsigned cp = hex4(i + 1);
            i += 4;
            // A surrogate pair is two escapes; fold them when they really pair.
            if(cp >= 0xD800 && cp <= 0xDBFF && i + 6 < s.size()
               && s[i + 1] == '\\' && s[i + 2] == 'u')
            {
              const unsigned lo = hex4(i + 3);
              if(lo >= 0xDC00 && lo <= 0xDFFF)
              {
                cp = 0x10000u + ((cp - 0xD800u) << 10) + (lo - 0xDC00u);
                i += 6;
              }
            }
            append_utf8(out, cp);
            break;
          }
          default: out += s[i]; break;
        }
      }
      else
      {
        out += s[i];
      }
      i++;
    }
    if(i >= s.size())
      throw std::runtime_error{"unterminated string"};
    i++;
    return out;
  }

  jk::value parse()
  {
    ws();
    if(i >= s.size())
      throw std::runtime_error{"eof"};
    if(lit("null"))
      return jk::value{jk::null_t{}};
    if(lit("true"))
      return jk::value{true};
    if(lit("false"))
      return jk::value{false};
    if(lit("nan"))
      return jk::value{std::nan("")};
    if(s[i] == '"')
      return jk::value{str()};

    if(s[i] == '[')
    {
      i++;
      jk::list_type l;
      ws();
      if(eat(']'))
        return jk::value{std::move(l)};
      while(true)
      {
        l.push_back(parse());
        if(eat(','))
          continue;
        if(eat(']'))
          break;
        throw std::runtime_error{"bad array"};
      }
      return jk::value{std::move(l)};
    }

    if(s[i] == '{')
    {
      i++;
      jk::map_type m;
      ws();
      if(eat('}'))
        return jk::value{std::move(m)};
      while(true)
      {
        auto k = str();
        if(!eat(':'))
          throw std::runtime_error{"bad object"};
        m[k] = parse();
        if(eat(','))
          continue;
        if(eat('}'))
          break;
        throw std::runtime_error{"bad object"};
      }
      return jk::value{std::move(m)};
    }

    const auto start = i;
    while(i < s.size()
          && (std::isdigit((unsigned char)s[i]) || s[i] == '-' || s[i] == '+'
              || s[i] == '.' || s[i] == 'e' || s[i] == 'E'))
      i++;
    if(i == start)
      throw std::runtime_error{"bad value"};
    const std::string num{s.substr(start, i - start)};
    if(num.find_first_of(".eE") != std::string::npos)
      return jk::number(std::stod(num));
    return jk::value{int64_t(std::stoll(num))};
  }
};
}

inline jk::value parse_json(std::string_view s)
{
  jk_test::json_reader r{s};
  return r.parse();
}
