#include <jk/actions.hpp>
#include <jk/builtins.hpp>
#include <jk/charconv.hpp>
#include <jk/memory.hpp>
#include <jk/parser.hpp>

#include <charconv>
#include <cstdlib>
#include <string>
#include <utility>

#include <string_view>

namespace jk::parser
{
namespace
{
action_fun literal(value v)
{
  return action_fun{[v = std::move(v)](const value&)
                    { return action::constant(v); }};
}

action_fun identity()
{
  return action_fun{[](const value& in) { return action::copy_all(in); }};
}

template <typename F>
action_fun combine(action_fun a, action_fun b, F operation)
{
  return action_fun{[a = std::move(a), b = std::move(b), operation](
                        const value& in) { return operation(in, a, b); }};
}

action_fun pipe(action_fun a, action_fun b)
{
  return combine(std::move(a), std::move(b), action::compose);
}

action_fun optional(action_fun a)
{
  return action_fun{[a = std::move(a)](const value& in)
                    { return action::optional(in, a); }};
}

action_fun index(action_fun base, action_fun key, bool opt)
{
  return action_fun{
      [base = std::move(base), key = std::move(key), opt](const value& in)
      { return action::index_by(in, base, key, opt); }};
}

action_fun arithmetic(action_fun a, action_fun b, char op)
{
  return combine(
      std::move(a),
      std::move(b),
      [op](const value& in, const action_fun& l, const action_fun& r)
      {
        return action::binary(
            in,
            l,
            r,
            [op](const value& x, const value& y)
            {
              switch (op)
              {
                case '+':
                  return jk::add(x, y);
                case '-':
                  return jk::subtract(x, y);
                case '*':
                  return jk::multiply(x, y);
                case '/':
                  return jk::divide(x, y);
                default:
                  return jk::modulo(x, y);
              }
            });
      });
}

void append_utf8(string_type& out, uint32_t cp)
{
  if (cp < 0x80)
    out += char(cp);
  else if (cp < 0x800)
  {
    out += char(0xC0 | (cp >> 6));
    out += char(0x80 | (cp & 0x3F));
  }
  else if (cp < 0x10000)
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

//! Recursive descent compiles each completed expression into a local action.
//! There is no global operand stack and no speculative semantic mutation: a
//! failed parse destroys its partial tree and cannot leak a malformed program.
class compiler
{
  std::string_view source;
  std::size_t pos{};

  [[noreturn]] void fail() const
  {
    throw error{"Syntax error at byte " + std::to_string(pos)};
  }

  static bool digit(char c) { return c >= '0' && c <= '9'; }
  static bool alpha(char c)
  {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
  }
  static bool identifier_char(char c) { return alpha(c) || digit(c); }
  char peek() const { return pos < source.size() ? source[pos] : '\0'; }

  void space()
  {
    for (;;)
    {
      while (peek() == ' ' || peek() == '\t' || peek() == '\r'
             || peek() == '\n')
        ++pos;
      if (peek() != '#')
        return;
      ++pos;
      while (pos < source.size())
      {
        if (peek() == '\n')
        {
          ++pos;
          break;
        }
        if (peek() == '\\')
        {
          ++pos;
          // Pair backslashes before deciding whether the newline continues
          // the comment. An odd run escapes LF or CRLF; an even run does not.
          if (peek() == '\\' || peek() == '\n')
          {
            ++pos;
            continue;
          }
          if (peek() == '\r' && pos + 1 < source.size()
              && source[pos + 1] == '\n')
          {
            pos += 2;
            continue;
          }
        }
        else
          ++pos;
      }
    }
  }

  bool take(std::string_view token)
  {
    space();
    if (source.substr(pos, token.size()) != token)
      return false;
    pos += token.size();
    return true;
  }

  bool keyword(std::string_view word)
  {
    space();
    if (source.substr(pos, word.size()) != word
        || (pos + word.size() < source.size()
            && identifier_char(source[pos + word.size()])))
      return false;
    pos += word.size();
    return true;
  }

  void expect(std::string_view token)
  {
    if (!take(token))
      fail();
  }
  void expect_keyword(std::string_view token)
  {
    if (!keyword(token))
      fail();
  }

  std::string_view identifier()
  {
    space();
    if (!alpha(peek()))
      fail();
    const auto start = pos++;
    while (identifier_char(peek()))
      ++pos;
    return source.substr(start, pos - start);
  }

  uint32_t hex4()
  {
    uint32_t cp = 0;
    for (int i = 0; i < 4; ++i)
    {
      char c = peek();
      unsigned d;
      if (digit(c))
        d = c - '0';
      else if (c >= 'a' && c <= 'f')
        d = c - 'a' + 10;
      else if (c >= 'A' && c <= 'F')
        d = c - 'A' + 10;
      else
        fail();
      ++pos;
      cp = (cp << 4) | d;
    }
    return cp;
  }

  action_fun string()
  {
    expect("\"");
    string_type text;
    std::optional<action_fun> result;
    auto append = [&](action_fun part)
    {
      if (result)
        *result = arithmetic(std::move(*result), std::move(part), '+');
      else
        result = std::move(part);
    };
    while (pos < source.size() && peek() != '"')
    {
      char c = source[pos++];
      if (c != '\\')
      {
        text += c;
        continue;
      }
      if (pos == source.size())
        fail();
      c = source[pos++];
      if (c == '(')
      {
        if (!text.empty())
        {
          append(literal(value{std::move(text)}));
          text.clear();
        }
        auto inner = expression();
        expect(")");
        append(pipe(std::move(inner), action::make_builtin("tostring", {})));
        continue;
      }
      switch (c)
      {
        case '"':
          text += '"';
          break;
        case '\\':
          text += '\\';
          break;
        case '/':
          text += '/';
          break;
        case 'b':
          text += '\b';
          break;
        case 'f':
          text += '\f';
          break;
        case 'n':
          text += '\n';
          break;
        case 'r':
          text += '\r';
          break;
        case 't':
          text += '\t';
          break;
        case 'u':
        {
          uint32_t cp = hex4();
          if (cp >= 0xD800 && cp <= 0xDBFF)
          {
            if (source.substr(pos, 2) != "\\u")
              fail();
            pos += 2;
            const auto low = hex4();
            if (low < 0xDC00 || low > 0xDFFF)
              fail();
            cp = 0x10000 + ((cp - 0xD800) << 10) + low - 0xDC00;
          }
          else if (cp >= 0xDC00 && cp <= 0xDFFF)
            cp = 0xFFFD;
          append_utf8(text, cp);
          break;
        }
        default:
          fail();
      }
    }
    // Do not skip comments or whitespace while closing a string.
    if (peek() != '"')
      fail();
    ++pos;
    if (!text.empty() || !result)
      append(literal(value{std::move(text)}));
    return std::move(*result);
  }

  action_fun number_literal()
  {
    const auto start = pos;
    while (digit(peek()))
      ++pos;
    if (peek() == '.')
    {
      ++pos;
      while (digit(peek()))
        ++pos;
    }
    if (peek() == 'e' || peek() == 'E')
    {
      ++pos;
      if (peek() == '+' || peek() == '-')
        ++pos;
      if (!digit(peek()))
        fail();
      while (digit(peek()))
        ++pos;
    }
    const auto token = source.substr(start, pos - start);
    int64_t integer;
    auto [end, ec]
        = std::from_chars(token.data(), token.data() + token.size(), integer);
    if (ec == std::errc{} && end == token.data() + token.size())
      return literal(value{integer});
    if (auto n = parse_number(token))
      return literal(value{*n});
    // strtod also handles overflow/underflow in jq's number-literal fashion.
    const std::string terminated{token};
    char* last{};
    const double n = std::strtod(terminated.c_str(), &last);
    if (last != terminated.c_str() + terminated.size())
      fail();
    return literal(value{n});
  }

  action_fun branch()
  {
    auto cond = expression();
    expect_keyword("then");
    auto yes = expression();
    action_fun no;
    if (keyword("elif"))
      no = branch();
    else if (keyword("else"))
    {
      no = expression();
      expect_keyword("end");
    }
    else
    {
      expect_keyword("end");
      no = identity();
    }
    return action_fun{[cond = std::move(cond),
                       yes = std::move(yes),
                       no = std::move(no)](const value& in)
                      { return action::conditional(in, cond, yes, no); }};
  }

  action_fun object()
  {
    std::vector<action::object_member> members;
    if (!take("}"))
    {
      for (;;)
      {
        space();
        action_fun key;
        bool computed = false;
        if (take("("))
        {
          key = expression();
          expect(")");
          computed = true;
        }
        else if (peek() == '"')
          key = string();
        else
          key = literal(value{string_type{identifier()}});
        action_fun val;
        const bool shorthand = !take(":");
        if (!shorthand)
          val = expression(false);
        else
        {
          if (computed)
            fail();
        }
        members.push_back({std::move(key), std::move(val), shorthand});
        if (take("}"))
          break;
        expect(",");
        if (take("}"))
          break;
      }
    }
    return action_fun{[members = std::move(members)](const value& in)
                      { return action::as_object(in, members); }};
  }

  action_fun bracket(action_fun base)
  {
    if (take("]"))
    {
      auto step = action_fun{[](const value& in)
                             { return action::iterate_array(in); }};
      if (take("?"))
        step = optional(std::move(step));
      return pipe(std::move(base), std::move(step));
    }
    action_fun key;
    if (take(":"))
    {
      auto end = expression();
      expect("]");
      auto start = literal(value{null_t{}});
      key = combine(std::move(start), std::move(end), action::slice_keys);
    }
    else
    {
      key = expression();
      if (take(":"))
      {
        auto end = literal(value{null_t{}});
        if (!take("]"))
        {
          end = expression();
          expect("]");
        }
        key = combine(std::move(key), std::move(end), action::slice_keys);
      }
      else
        expect("]");
    }
    const bool opt = take("?");
    return index(std::move(base), std::move(key), opt);
  }

  action_fun primary()
  {
    space();
    if (keyword("if"))
      return branch();
    if (keyword("try"))
    {
      auto body = unary();
      if (!keyword("catch"))
        return optional(std::move(body));
      auto handler = unary();
      return combine(std::move(body), std::move(handler), action::try_catch);
    }
    if (keyword("null"))
      return literal(value{null_t{}});
    if (keyword("true"))
      return literal(value{true});
    if (keyword("false"))
      return literal(value{false});
    if (digit(peek())
        || (peek() == '.' && pos + 1 < source.size()
            && digit(source[pos + 1])))
      return number_literal();
    if (peek() == '"')
      return string();
    if (take("{"))
      return object();
    if (take("["))
    {
      if (take("]"))
        return literal(value{list_type{}});
      auto body = expression();
      expect("]");
      return action_fun{[body = std::move(body)](const value& in)
                        { return action::as_array(in, body); }};
    }
    if (take("("))
    {
      auto result = expression();
      expect(")");
      return result;
    }
    if (take(".."))
      return action_fun{[](const value& in) { return action::recurse(in); }};
    if (take("."))
    {
      auto base = identity();
      const bool adjacent_field = alpha(peek());
      space();
      if (take("["))
        return bracket(std::move(base));
      if (peek() == '"' || adjacent_field)
      {
        auto key = peek() == '"' ? string()
                                 : literal(value{string_type{identifier()}});
        const bool opt = take("?");
        return index(std::move(base), std::move(key), opt);
      }
      return base;
    }
    auto name = identifier();
    std::vector<action_fun> args;
    if (take("("))
    {
      // Empty call parentheses are not jq syntax; arity zero is a bare name.
      args.push_back(expression());
      while (take(";"))
        args.push_back(expression());
      expect(")");
    }
    return action::make_builtin(name, std::move(args));
  }

  action_fun postfix()
  {
    auto result = primary();
    for (;;)
    {
      if (take("["))
        result = bracket(std::move(result));
      else if (take("."))
      {
        const bool adjacent_field = alpha(peek());
        if (take("["))
          result = bracket(std::move(result));
        else
        {
          space();
          if (peek() != '"' && !adjacent_field)
            fail();
          auto key = peek() == '"' ? string()
                                   : literal(value{string_type{identifier()}});
          const bool opt = take("?");
          result = index(std::move(result), std::move(key), opt);
        }
      }
      else if (take("?"))
        result = optional(std::move(result));
      else
        return result;
    }
  }

  action_fun unary()
  {
    if (take("-"))
    {
      auto body = unary();
      return action_fun{[body = std::move(body)](const value& in)
                        { return action::negate(in, body); }};
    }
    return postfix();
  }

  // Low to high: pipe, comma, alternative, or, and, comparison, add, multiply.
  // The comma flag only changes the object-value level; nested expressions
  // and function arguments always retain comma's full stream semantics.
  action_fun expression(bool comma = true, int minimum = 1)
  {
    auto lhs = unary();
    bool compared = false;
    for (;;)
    {
      space();
      const auto start = pos;
      std::string_view op;
      int precedence = 0;
      if (source.substr(pos, 2) == "//")
      {
        op = "//";
        precedence = 3;
      }
      else if (
          source.substr(pos, 2) == "==" || source.substr(pos, 2) == "!="
          || source.substr(pos, 2) == "<=" || source.substr(pos, 2) == ">=")
      {
        op = source.substr(pos, 2);
        precedence = 6;
      }
      else if (peek() == '|')
      {
        op = "|";
        precedence = 1;
      }
      else if (peek() == ',' && comma)
      {
        op = ",";
        precedence = 2;
      }
      else if (peek() == '<' || peek() == '>')
      {
        op = source.substr(pos, 1);
        precedence = 6;
      }
      else if (peek() == '+' || peek() == '-')
      {
        op = source.substr(pos, 1);
        precedence = 7;
      }
      else if (peek() == '*' || peek() == '/' || peek() == '%')
      {
        op = source.substr(pos, 1);
        precedence = 8;
      }
      else if (keyword("or"))
      {
        op = "or";
        precedence = 4;
      }
      else if (keyword("and"))
      {
        op = "and";
        precedence = 5;
      }
      pos = start;
      if (precedence < minimum)
        return lhs;
      if (precedence == 6 && compared)
        fail();
      compared = precedence == 6;
      pos += op.size();
      // Pipe and alternative associate right; all other operators left.
      auto rhs = expression(
          comma, precedence + (precedence != 1 && precedence != 3));
      if (op == "|")
        lhs = pipe(std::move(lhs), std::move(rhs));
      else if (op == ",")
        lhs = combine(std::move(lhs), std::move(rhs), action::concat);
      else if (op == "//")
        lhs = combine(std::move(lhs), std::move(rhs), action::alternative);
      else if (op == "or")
        lhs = combine(std::move(lhs), std::move(rhs), action::logical_or);
      else if (op == "and")
        lhs = combine(std::move(lhs), std::move(rhs), action::logical_and);
      else if (op == "==" || op == "!=")
      {
        lhs = combine(
            std::move(lhs),
            std::move(rhs),
            [negate = op == "!="](
                const value& in, const action_fun& a, const action_fun& b)
            { return action::equality(in, a, b, negate); });
      }
      else if (precedence == 6)
      {
        lhs = combine(
            std::move(lhs),
            std::move(rhs),
            [less = op[0] == '<', inclusive = op.size() == 2](
                const value& in, const action_fun& a, const action_fun& b)
            {
              return action::comparison(
                  in,
                  a,
                  b,
                  [less, inclusive](int c)
                  { return (less ? c < 0 : c > 0) || (inclusive && c == 0); });
            });
      }
      else
        lhs = arithmetic(std::move(lhs), std::move(rhs), op[0]);
    }
  }

public:
  explicit compiler(std::string_view text)
      : source{text}
  {
  }

  action_fun compile()
  {
    auto result = expression();
    space();
    if (pos != source.size())
      fail();
    return result;
  }
};
}
}

namespace jk
{
std::optional<actions::handlers> parse(std::string_view str)
{
  allocation_scope compile_scope{nullptr};
  try
  {
    actions::handlers result;
    result.current_seq.push_back(parser::compiler{str}.compile());
    return result;
  }
  catch (const jk::error&)
  {
    return std::nullopt;
  }
}
}
