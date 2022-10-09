#include <boost/spirit/home/x3.hpp>

#include <jk/action_handler.hpp>
#include <jk/actions.hpp>
#include <jk/parser.hpp>

// clang-format off
namespace jk::parser {
using namespace boost::spirit;

static const x3::rule<struct id_action> action = "action";
static const x3::rule<struct id_array> array = "array";
static const x3::rule<struct id_empty_array> empty_array = "empty_array";
static const x3::rule<struct id_atom> atom = "atom";
static const x3::rule<struct id_pipe> pipe = "pipe";
static const x3::rule<struct id_seq> seq = "seq";
static const x3::rule<struct id_root> root = "root";
// https://stackoverflow.com/questions/72278861/spirit-x3-passing-local-data-to-a-parser/72280079#72280079
// thanks @sehe !
#define EVENT(e) ([](auto& ctx) { x3::get<actions::handlers>(ctx).e(x3::_attr(ctx)); })

static const auto action_def =   array
                        | (+('.'                                                                           // .
                           >> -((+x3::alnum)[EVENT(access_member)])                                        // .foo
                           >> *(  ('[' >> x3::int_ >> ']')[EVENT(access_array)]                            // .[1]
                                | ('[' >> (x3::int_ % ',') >> ']')[EVENT(access_array_pipe)]           // .[1, 2]
                                | ('[' >> x3::int_ >> ':' >> x3::int_ >> ']')[EVENT(access_array_range)]   // .[1:2]
                                | x3::lit("[]")[EVENT(access_empty_array)])                                // .[]
                               )
                          );
static const auto pipe_def = (action[EVENT(finish_pipe_action)] % '|')[EVENT(finish_pipe)];// |  (action[EVENT(finish_action)] % ',')[EVENT(finish_pipe)];
static const auto seq_def = (pipe[EVENT(finish_seq_action)] % ',')[EVENT(finish_seq)];// |  (action[EVENT(finish_action)] % ',')[EVENT(finish_pipe)];
static const auto array_def = ('[' >> seq >> ']')[EVENT(create_array)];
static const auto root_def = array | action;

BOOST_SPIRIT_DEFINE(action)
BOOST_SPIRIT_DEFINE(array)
BOOST_SPIRIT_DEFINE(pipe)
BOOST_SPIRIT_DEFINE(seq)
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
  bool res = phrase_parse(
      first,
      last,
      boost::spirit::x3::with<actions::handlers>(r)[parser::seq_def],
      boost::spirit::x3::ascii::space);

  if (!res || first != last)
    return std::nullopt;
  return r;
}
}
