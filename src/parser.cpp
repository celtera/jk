#include <jk/parser.hpp>
#include <jk/actions.hpp>
#include <jk/action_handler.hpp>

#include <boost/spirit/home/x3.hpp>

namespace jk::parser {
using namespace boost::spirit;

x3::rule<struct id_action> action = "action";
x3::rule<struct id_array> array = "array";
x3::rule<struct id_empty_array> empty_array = "empty_array";
x3::rule<struct id_atom> atom = "atom";
x3::rule<struct id_sequence> sequence = "sequence";
x3::rule<struct id_root> root = "root";

// https://stackoverflow.com/questions/72278861/spirit-x3-passing-local-data-to-a-parser/72280079#72280079
// thanks @sehe !
#define EVENT(e) ([](auto& ctx) { x3::get<actions::handlers>(ctx).e(x3::_attr(ctx)); })

const auto action_def =   array
                        | (+('.'                                                                           // .
                           >> -((+x3::alnum)[EVENT(access_member)])                                        // .foo
                           >> *(  ('[' >> x3::int_ >> ']')[EVENT(access_array)]                            // .[1]
                                | ('[' >> (x3::int_ % ',') >> ']')[EVENT(access_array_sequence)]           // .[1, 2]
                                | ('[' >> x3::int_ >> ':' >> x3::int_ >> ']')[EVENT(access_array_range)]   // .[1:2]
                                | x3::lit("[]")[EVENT(access_empty_array)])                                // .[]
                               )
                          );
const auto sequence_def = (action[EVENT(finish_action)] % '|')[EVENT(finish_sequence)];
const auto array_def = ('[' >> (sequence % ',') >> ']')[EVENT(create_array)];
const auto root_def = array | action;

BOOST_SPIRIT_DEFINE(action)
BOOST_SPIRIT_DEFINE(array)
BOOST_SPIRIT_DEFINE(sequence)
BOOST_SPIRIT_DEFINE(root)
}

namespace jk {
std::optional<actions::handlers> parse(std::string_view str)
{
  actions::handlers r;
  auto first = str.begin();
  auto last = str.end();
  bool res = phrase_parse(
             first, last,
             boost::spirit::x3::with<actions::handlers>(r)[ parser::sequence_def ],
             boost::spirit::x3::ascii::space
  );

  if (!res || first != last)
    return std::nullopt;
  return r;
}
}
