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
    for (auto& v : action::process_sequence(input, res->total))
    {
      visit(print{}, v.data.v);
      std::cerr << "\n";
    }
  }
  std::cerr << std::endl;
}
