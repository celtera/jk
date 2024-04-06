#include <boost/spirit/home/x3.hpp>

#include <jk/action_handler.hpp>
#include <jk/actions.hpp>
#include <jk/parser.hpp>

// clang-format off
namespace jk::parser {
using namespace boost::spirit;

static const x3::rule<struct id_action> action = "action";
static const x3::rule<struct id_array> array = "array";
static const x3::rule<struct id_object> object = "object";
static const x3::rule<struct id_empty_array> empty_array = "empty_array";
static const x3::rule<struct id_atom> atom = "atom";
static const x3::rule<struct id_pipe> pipe = "pipe";
static const x3::rule<struct id_seq> seq = "seq";
static const x3::rule<struct id_obj_mem> obj_mem = "obj_mem";
static const x3::rule<struct id_obj_seq> obj_seq = "obj_seq";
static const x3::rule<struct id_root> root = "root";
// https://stackoverflow.com/questions/72278861/spirit-x3-passing-local-data-to-a-parser/72280079#72280079
// thanks @sehe !
#define EVENT(e) ([](auto& ctx) { x3::get<actions::handlers>(ctx).e(x3::_attr(ctx)); })

// https://stackoverflow.com/questions/74031183/cleanest-way-to-handle-both-quoted-and-unquoted-strings-in-spirit-x3
// https://stackoverflow.com/questions/33578440/boostspirit-alnum-p-and-hyphen
template <typename T> struct as_type {
  auto operator()(auto p) const { return x3::rule<struct Tag, T>{} = p; }
};
static constexpr as_type<std::string> as_string{};

static const auto identifier_char = x3::alnum | x3::char_('_');
static const auto unquoted_identifier  = +identifier_char;
static const auto quoted_identifier  = '"' >> x3::no_skip[*~x3::char_('"')] >> '"';
static const auto identifier    = as_string(unquoted_identifier | quoted_identifier);
static const auto action_def = array | object
                        | x3::lit("..")[EVENT(recurse)]                                                    // ..
                        | (+('.'                                                                           // .
                           >> -((identifier)[EVENT(access_member)])                                        // .foo
                           >> *(  ('[' >> x3::int_ >> ']')[EVENT(access_array)]                            // .[1]
                                | ('[' >> (x3::int_ % ',') >> ']')[EVENT(access_array_pipe)]               // .[1, 2]
                                | ('[' >> x3::int_ >> ':' >> x3::int_ >> ']')[EVENT(access_array_range)]   // .[1:2]
                                | x3::lit("[]")[EVENT(access_empty_array)])                                // .[]
                               )
                          );
static const auto pipe_def = (action[EVENT(finish_pipe_action)] % '|')[EVENT(finish_pipe)];
static const auto seq_def = (pipe[EVENT(finish_seq_action)] % ',')[EVENT(finish_seq)];
static const auto obj_mem_def = (((identifier >> ':' >> action)[EVENT(create_member)] | ((identifier)[EVENT(read_member)] )) % ',');
static const auto obj_seq_def = (obj_mem % ',');
static const auto array_def = ('[' >> seq >> ']')[EVENT(create_array)];
static const auto object_def = ('{' >> obj_seq >> '}')[EVENT(create_object)];
static const auto root_def = array | action;

BOOST_SPIRIT_DEFINE(action)
BOOST_SPIRIT_DEFINE(array)
BOOST_SPIRIT_DEFINE(object)
BOOST_SPIRIT_DEFINE(pipe)
BOOST_SPIRIT_DEFINE(seq)
BOOST_SPIRIT_DEFINE(obj_mem)
BOOST_SPIRIT_DEFINE(obj_seq)
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
  bool res = phrase_parse(first,
                          last,
                          boost::spirit::x3::with<actions::handlers>(r)[parser::seq_def],
                          boost::spirit::x3::ascii::space,
                          boost::spirit::x3::skip_flag::post_skip);

  if (!res || first != last)
    return std::nullopt;
  return r;
}
}
