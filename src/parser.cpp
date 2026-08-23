#include <boost/spirit/home/x3.hpp>

#include <jk/action_handler.hpp>
#include <jk/actions.hpp>
#include <jk/parser.hpp>

// clang-format off
namespace jk::parser {
using namespace boost::spirit;

// Precedence, loosest first, as in jq's own grammar:
//
//   |            pipe
//   ,            comma
//   //           alternative
//   or
//   and
//   == != < <= > >=
//   + -
//   * / %
//   -            unary minus
//   postfix      .foo  .[...]  ?
//   primary      literals, ( ), [ ], { }, builtins
//
// The previous grammar had , and | the other way round, which made
// `.[] | .a, .b` mean `(.[] | .a), .b` instead of jq's `.[] | (.a, .b)`.
static const x3::rule<struct id_pipe_expr> pipe_expr = "pipe_expr";
static const x3::rule<struct id_comma_expr> comma_expr = "comma_expr";
static const x3::rule<struct id_alt_expr> alt_expr = "alt_expr";
static const x3::rule<struct id_or_expr> or_expr = "or_expr";
static const x3::rule<struct id_and_expr> and_expr = "and_expr";
static const x3::rule<struct id_cmp_expr> cmp_expr = "cmp_expr";
static const x3::rule<struct id_add_expr> add_expr = "add_expr";
static const x3::rule<struct id_mul_expr> mul_expr = "mul_expr";
static const x3::rule<struct id_unary> unary = "unary";
static const x3::rule<struct id_postfix> postfix = "postfix";
static const x3::rule<struct id_primary> primary = "primary";
static const x3::rule<struct id_dot_expr> dot_expr = "dot_expr";
static const x3::rule<struct id_suffix> suffix = "suffix";
static const x3::rule<struct id_object> object = "object";
static const x3::rule<struct id_obj_member> obj_member = "obj_member";
static const x3::rule<struct id_root> root = "root";

#define EVENT(e) ([](auto& ctx) { x3::get<actions::handlers>(ctx).e(x3::_attr(ctx)); })

template <typename T> struct as_type {
  auto operator()(auto p) const { return x3::rule<struct Tag, T>{} = p; }
};
static constexpr as_type<std::string> as_string{};

static const auto identifier_char = x3::alnum | x3::char_('_');
static const auto unquoted_identifier
    = x3::lexeme[(x3::alpha | x3::char_('_')) >> *identifier_char];

// Encode one codepoint as UTF-8. jq's strings are UTF-8, and \uXXXX has to
// land as the bytes for that character rather than as the escape text.
inline void append_utf8(std::string& out, uint32_t cp)
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

// The escapes JSON defines. x3::attr supplies the decoded character, which
// keeps this rule's attribute a plain char rather than a variant.
static const auto simple_escape
    = x3::rule<struct id_esc, char>{}
    = ('"'  >> x3::attr('"'))
    | ('\\' >> x3::attr('\\'))
    | ('/'  >> x3::attr('/'))
    | ('n'  >> x3::attr('\n'))
    | ('t'  >> x3::attr('\t'))
    | ('r'  >> x3::attr('\r'))
    | ('b'  >> x3::attr('\b'))
    | ('f'  >> x3::attr('\f'));

static const auto hex4 = x3::uint_parser<uint32_t, 16, 4, 4>{};

static const auto append_char = [](auto& ctx) { x3::_val(ctx) += x3::_attr(ctx); };

// \uXXXX, and the surrogate pair that an astral character is written as. A
// second escape is only folded in when the two really are a high/low pair;
// otherwise both are emitted as they stand.
static const auto append_codepoint = [](auto& ctx) {
  auto& att = x3::_attr(ctx);
  const uint32_t hi = boost::fusion::at_c<0>(att);
  const auto& lo_opt = boost::fusion::at_c<1>(att);
  std::string& out = x3::_val(ctx);

  if(lo_opt)
  {
    const uint32_t lo = *lo_opt;
    if(hi >= 0xD800 && hi <= 0xDBFF && lo >= 0xDC00 && lo <= 0xDFFF)
    {
      append_utf8(out, 0x10000u + ((hi - 0xD800u) << 10) + (lo - 0xDC00u));
      return;
    }
    append_utf8(out, hi);
    append_utf8(out, lo);
    return;
  }
  append_utf8(out, hi);
};

// A backslash that starts none of the escapes above fails the whole string,
// as it does in jq: "\q" is a syntax error, not a literal backslash-q. That
// also means an interpolation - "\(...)", which jk does not implement - is
// rejected rather than silently read as text.
static const x3::rule<struct id_str_body, std::string> string_body = "string_body";
static const auto string_body_def
    = x3::lexeme[
        x3::lit('"')
     >> *( (x3::lit("\\u") >> hex4 >> -(x3::lit("\\u") >> hex4))[append_codepoint]
         | (x3::lit('\\') >> simple_escape)[append_char]
         | (~x3::char_("\"\\"))[append_char] )
     >> x3::lit('"')];

static const auto string_lit = string_body;

// A field name after a dot may be bare or quoted: .foo and ."foo bar". Both
// arms go through as_string, or the alternative's attribute is a variant of
// two strings rather than a string.
static const auto field_name = as_string(unquoted_identifier | string_body);

// A keyword must not swallow the start of a longer identifier: `and` is an
// operator, but `.android` is a field and `android` a function name.
static const auto kw = [](const char* k) {
  return x3::lexeme[x3::lit(k) >> !identifier_char];
};

// An integer stays an integer, so that it prints back as one. strict_real
// only matches when there is a '.' or an exponent, so "1" falls through to the
// integer parser rather than becoming 1.0.
static const auto number_lit
    = x3::real_parser<double, x3::strict_real_policies<double>>{}[EVENT(lit_double)]
    | x3::long_long[EVENT(lit_int)];

static const auto index_suffix
    = (x3::lit('[') >> x3::lit(']'))[EVENT(iterate)]
    | (x3::lit('[') >> (x3::int_ >> ':' >> x3::int_) >> ']')[EVENT(access_array_range)]
    | (x3::lit('[') >> x3::int_ >> ':' >> ']')[EVENT(access_array_range_from)]
    | (x3::lit('[') >> ':' >> x3::int_ >> ']')[EVENT(access_array_range_to)]
    // A single index is the one-element case of .[a, b] and behaves the same,
    // so one rule covers both.
    | (x3::lit('[') >> (x3::int_ % ',') >> ']')[EVENT(access_array_pipe)]
    | (x3::lit('[') >> pipe_expr >> ']')[EVENT(index_by_expr)];

static const auto suffix_def
    = (x3::lit('.') >> field_name)[EVENT(access_member)]
    | index_suffix
    | x3::lit('?')[EVENT(optional)];

// `.`, `.foo`, `.foo.bar`, `.[0]`, `.foo[0].bar` ... The leading dot belongs
// to the primary; the first field name follows it directly, later ones bring
// their own dot and are handled by the suffix rule.
static const auto dot_expr_def
    = x3::lit('.')[EVENT(identity)]
   >> -(field_name[EVENT(access_member)] | index_suffix)
   >> *suffix;

static const auto obj_member_def
    = ((field_name >> ':' >> alt_expr)[EVENT(object_member)])
    | ((x3::lit('(') >> pipe_expr >> ')' >> ':' >> alt_expr)[EVENT(object_member_expr)])
    | (field_name[EVENT(object_shorthand)]);

static const auto object_def
    = (x3::lit('{')[EVENT(begin_object)] >> -(obj_member % ',') >> '}')[EVENT(create_object)];

static const auto primary_def
    = kw("null")[EVENT(lit_null)]
    | kw("true")[EVENT(lit_true)]
    | kw("false")[EVENT(lit_false)]
    | number_lit
    | string_lit[EVENT(lit_string)]
    | object
    | (x3::lit('[') >> x3::lit(']'))[EVENT(empty_array)]
    | (x3::lit('[') >> pipe_expr >> ']')[EVENT(create_array)]
    | (x3::lit('(') >> pipe_expr >> ')')
    | x3::lit("..")[EVENT(recurse)]
    | dot_expr
    // A bare name is a function call; with parentheses it takes an argument.
    | (as_string(unquoted_identifier) >> '(' >> pipe_expr >> ')')[EVENT(builtin1)]
    | as_string(unquoted_identifier)[EVENT(builtin0)];

static const auto postfix_def = primary >> *suffix;

static const auto unary_def
    = (x3::lit('-') >> unary)[EVENT(op_neg)]
    | postfix;

// `//` is the alternative operator, so a division must not consume its first
// slash.
static const auto mul_expr_def = unary >> *( ('*' >> unary)[EVENT(op_mul)]
                                           | (x3::lit('/') >> !x3::lit('/') >> unary)[EVENT(op_div)]
                                           | ('%' >> unary)[EVENT(op_mod)] );

static const auto add_expr_def = mul_expr >> *( ('+' >> mul_expr)[EVENT(op_add)]
                                              | ('-' >> mul_expr)[EVENT(op_sub)] );

// Comparison does not chain in jq: `a < b < c` is a syntax error there too.
static const auto cmp_expr_def = add_expr >> -( (x3::lit("==") >> add_expr)[EVENT(op_eq)]
                                              | (x3::lit("!=") >> add_expr)[EVENT(op_ne)]
                                              | (x3::lit("<=") >> add_expr)[EVENT(op_le)]
                                              | (x3::lit(">=") >> add_expr)[EVENT(op_ge)]
                                              | (x3::lit('<') >> add_expr)[EVENT(op_lt)]
                                              | (x3::lit('>') >> add_expr)[EVENT(op_gt)] );

static const auto and_expr_def = cmp_expr >> *( (kw("and") >> cmp_expr)[EVENT(op_and)] );
static const auto or_expr_def  = and_expr >> *( (kw("or") >> and_expr)[EVENT(op_or)] );
static const auto alt_expr_def = or_expr >> *( (x3::lit("//") >> or_expr)[EVENT(alternative)] );
static const auto comma_expr_def = alt_expr >> *( (',' >> alt_expr)[EVENT(comma)] );
static const auto pipe_expr_def = comma_expr >> *( ('|' >> comma_expr)[EVENT(pipe)] );
static const auto root_def = pipe_expr[EVENT(finish)];

BOOST_SPIRIT_DEFINE(string_body)
BOOST_SPIRIT_DEFINE(pipe_expr)
BOOST_SPIRIT_DEFINE(comma_expr)
BOOST_SPIRIT_DEFINE(alt_expr)
BOOST_SPIRIT_DEFINE(or_expr)
BOOST_SPIRIT_DEFINE(and_expr)
BOOST_SPIRIT_DEFINE(cmp_expr)
BOOST_SPIRIT_DEFINE(add_expr)
BOOST_SPIRIT_DEFINE(mul_expr)
BOOST_SPIRIT_DEFINE(unary)
BOOST_SPIRIT_DEFINE(postfix)
BOOST_SPIRIT_DEFINE(primary)
BOOST_SPIRIT_DEFINE(dot_expr)
BOOST_SPIRIT_DEFINE(suffix)
BOOST_SPIRIT_DEFINE(object)
BOOST_SPIRIT_DEFINE(obj_member)
BOOST_SPIRIT_DEFINE(root)

#undef EVENT
}
// clang-format on
namespace jk
{
std::optional<actions::handlers> parse(std::string_view str)
{
  actions::handlers r;
  auto first = str.begin();
  auto last = str.end();

  try
  {
    const bool res = phrase_parse(
        first,
        last,
        boost::spirit::x3::with<actions::handlers>(r)[parser::root_def],
        boost::spirit::x3::ascii::space,
        boost::spirit::x3::skip_flag::post_skip);

    if (!res || first != last)
      return std::nullopt;
  }
  catch (const jk::error&)
  {
    // An unknown function name: reject the program rather than let it load
    // and then quietly do nothing.
    return std::nullopt;
  }
  return r;
}
}
