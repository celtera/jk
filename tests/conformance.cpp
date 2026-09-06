// Conformance against jq itself.
//
// Every case stores jq's ordered output and terminal status separately, so an
// unsupported program cannot pass as a runtime error, nor lose earlier outputs.
// tests/gen-cases.sh regenerates the table from tests/cases.txt, so the
// expectations are never written by hand.
//
// Object keys are compared sorted (jq -S), because jk holds objects in an
// ordered map while jq preserves insertion order.
#include "test_json.hpp"

#include <jk/actions.hpp>
#include <jk/parser.hpp>
#include <jk/print.hpp>

#include <cstdio>
#include <string>
#include <vector>

#include <string_view>

namespace
{
int failures = 0;
int checked = 0;

struct Case
{
  const char* program;
  const char* input;
  const char* expected; // JSON values separated by '\n'
  int status;           // jq: 0 success, 3 compile error, 5 runtime error
};

struct Outcome
{
  std::string output;
  int status = 0;
};

Outcome run(const char* program, const char* input_json)
{
  Outcome out;
  try
  {
    auto in = parse_json(input_json);
    auto prog = jk::parse(program);
    if (!prog)
      return {{}, 3};

    for (auto& v : jk::action::process_sequence(in, prog->current_seq))
    {
      if (!out.output.empty())
        out.output += '\n';
      jk::render_json(v.get(), out.output);
    }
  }
  catch (const jk::error&)
  {
    out.status = 5;
  }
  catch (const std::exception& e)
  {
    std::printf("Unexpected exception: %s\n", e.what());
    out.status = -1;
  }
  return out;
}

void check(const Case& c)
{
  ++checked;
  const auto got = run(c.program, c.input);
  const std::string want = c.expected;

  if (got.output != want || got.status != c.status)
  {
    ++failures;
    std::printf("FAIL  %s\n", c.program);
    std::printf("      input:    %s\n", c.input);
    std::printf("      expected (status %d): %s\n", c.status, want.c_str());
    std::printf(
        "      got      (status %d): %s\n", got.status, got.output.c_str());
  }
}
}

int main()
{
  std::setvbuf(stdout, nullptr, _IONBF, 0);

  static const Case cases[] = {
#include "conformance_cases.inc"
  };

  for (const auto& c : cases)
    check(c);

  std::printf("\n%d case(s), %d failure(s)\n", checked, failures);
  return failures != 0;
}
