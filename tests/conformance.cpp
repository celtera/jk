// Conformance against jq itself.
//
// Every case here was run through jq and the expected column is jq's own
// output, one line per value it produced; `<error>` means jq exited non-zero.
// tests/gen-cases.sh regenerates the table from tests/cases.txt, so the
// expectations are never written by hand.
//
// Object keys are compared sorted (jq -S), because jk holds objects in an
// ordered map while jq preserves insertion order.
#include <jk/parser.hpp>
#include <jk/print.hpp>

#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

#include "test_json.hpp"

namespace
{
int failures = 0;
int checked = 0;

struct Case
{
  const char* program;
  const char* input;
  const char* expected; // values separated by '\n', or "<error>"
};

//! Run one program and render its output the way the table stores it.
std::string run(const char* program, const char* input_json)
{
  jk::value in;
  try
  {
    in = parse_json(input_json);
  }
  catch(...)
  {
    return "<bad test input>";
  }

  auto prog = jk::parse(program);
  if(!prog)
    return "<error>";

  std::string out;
  try
  {
    bool first = true;
    for(auto& v : jk::action::process_sequence(in, prog->current_seq))
    {
      if(!first)
        out += '\n';
      first = false;
      jk::render_json(v.data, out);
    }
  }
  catch(...)
  {
    return "<error>";
  }
  return out;
}

void check(const Case& c)
{
  ++checked;
  const std::string got = run(c.program, c.input);
  const std::string want = c.expected;

  if(got != want)
  {
    ++failures;
    std::printf("FAIL  %s\n", c.program);
    std::printf("      input:    %s\n", c.input);
    std::printf("      expected: %s\n", want.c_str());
    std::printf("      got:      %s\n", got.c_str());
  }
}
}

int main()
{
  std::setvbuf(stdout, nullptr, _IONBF, 0);

  static const Case cases[] = {
#include "conformance_cases.inc"
  };

  for(const auto& c : cases)
    check(c);

  std::printf("\n%d case(s), %d failure(s)\n", checked, failures);
  return failures != 0;
}
