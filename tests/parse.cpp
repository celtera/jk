#include <jk/parser.hpp>

int main()
{
  using jk::parser::parse;

  assert(parse(". "));
  assert(parse(". [] "));
  assert(parse(". [12] "));
  assert(parse(". [12][3] "));
  assert(parse(". [0] [12]"));
  assert(parse(". [12] | . "));
  assert(parse(". [12] | . [3] "));
  assert(parse(". [12] | . [] "));
  assert(parse(". [] | .[12]"));
  assert(parse(". [0] | .[12]"));
  assert(parse(" . | . "));
  assert(parse("[ . ] | . "));
  assert(parse("[ . ] | [ . ]"));
  assert(parse(" .  | [ . ]"));
  assert(parse("[. [0] | .[12]]"));
  assert(parse("[. [0] ] | [ .[12] ]"));
  assert(parse(" [ . ] | . "));

  assert(parse(".foo"));
  assert(parse(".foo.bar"));
  assert(parse(".foo[0].bar"));
  assert(parse(".foo[0].bar[1][2]"));
  assert(parse(".foo[0].bar[][2]"));
  assert(parse("[ .[1], .[3] ]"));
  assert(parse("[ .[][0:2] ]"));
  assert(parse("[ .[][0, 2] ]"));
  assert(parse("[ .[][0, 2] ] | .[0]"));
  assert(parse("[ [ .[][0] ], [ .[][1] ] ]"));

  return 0;
}
