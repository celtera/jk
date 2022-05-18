#include <jk/parser.hpp>
#include <jk/print.hpp>

int main()
{
  using namespace std::literals;
  using namespace jk;
  value input{ list_type {
        value{ list_type{ "foo"s, 1, 3, 2.5 } }
      , value{ list_type{ "bar"s, 4, 7, 9 } }
    }
  };
  visit(print{}, input.v);

  std::cerr << "\ngives :\n";

  if(auto res = parser::parse("[ . [][1] ]")) {
    for(auto&v : action::collapse(input, res->total)) {
      visit(print{}, v.data.v);
      std::cerr << "\n";
    }
  }
  std::cerr << std::endl;
}