#include <jk/actions.hpp>
#include <jk/parser.hpp>
#include <jk/print.hpp>

int main()
{
  using namespace std::literals;
  using namespace jk;
  value input{123};
  visit(print{}, input.v);

  std::cerr << "\ngives :\n";

  if (auto res = parse("., ., ."))
  {
    for (auto& v : action::process_sequence(input, res->current_seq))
    {
      visit(print{}, v.get().v);
      std::cerr << "\n";
    }
  }
  std::cerr << std::endl;
}
