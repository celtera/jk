// Run the *official* jq test suite (jq's tests/jq.test) against jk.
//
//   jq_suite path/to/jq.test [-v]
//
// jk implements a subset of jq, so a program that fails to parse is reported
// as UNSUPPORTED rather than as a failure: it means the feature is not there
// yet, which is a known and deliberate gap. What must stay at zero is
// MISMATCH - a program jk accepted and then answered differently from jq.
//
// The file's format is groups of lines separated by blanks: the program, the
// input, then one line per expected output. `%%FAIL` marks a program jq itself
// rejects.
#include "test_json.hpp"

#include <jk/actions.hpp>
#include <jk/parser.hpp>
#include <jk/print.hpp>

#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

namespace
{
struct Group
{
  std::string program;
  std::string input;
  std::vector<std::string> expected;
  bool expect_parse_failure = false;
};

std::vector<Group> load(const char* path)
{
  std::vector<Group> out;
  std::ifstream f{path};
  if (!f)
    return out;

  std::string line;
  std::vector<std::string> block;
  bool fail_marker = false;

  const auto flush = [&]
  {
    if (block.size() >= 1)
    {
      Group g;
      g.expect_parse_failure = fail_marker;
      g.program = block[0];
      if (block.size() >= 2)
        g.input = block[1];
      for (std::size_t i = 2; i < block.size(); i++)
        g.expected.push_back(block[i]);
      out.push_back(std::move(g));
    }
    block.clear();
    fail_marker = false;
  };

  while (std::getline(f, line))
  {
    if (!line.empty() && line.back() == '\r')
      line.pop_back();

    if (line.rfind("%%FAIL", 0) == 0)
    {
      // The marker applies to the group that follows it.
      flush();
      fail_marker = true;
      continue;
    }
    if (!line.empty() && line[0] == '#')
      continue;
    if (line.empty())
    {
      flush();
      continue;
    }
    block.push_back(line);
  }
  flush();
  return out;
}
}

int main(int argc, char** argv)
{
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  if (argc < 2)
  {
    std::printf("usage: jq_suite <jq.test> [-v]\n");
    return 2;
  }
  const bool verbose = argc > 2 && std::strcmp(argv[2], "-v") == 0;

  const auto groups = load(argv[1]);
  if (groups.empty())
  {
    std::printf("could not read %s\n", argv[1]);
    return 2;
  }

  int pass = 0, mismatch = 0, unsupported = 0, bad_input = 0, rejected_ok = 0;

  for (const auto& g : groups)
  {
    auto prog = jk::parse(g.program);

    if (g.expect_parse_failure)
    {
      // jq rejects these; jk rejecting them too is the right answer, and
      // accepting them is a lenience worth knowing about but not a wrong
      // result.
      if (!prog)
        ++rejected_ok;
      else if (verbose)
        std::printf("LENIENT   %s\n", g.program.c_str());
      continue;
    }

    if (!prog)
    {
      ++unsupported;
      if (verbose)
        std::printf("UNSUPPORTED  %s\n", g.program.c_str());
      continue;
    }

    jk::value in;
    try
    {
      in = parse_json(g.input);
    }
    catch (...)
    {
      ++bad_input;
      continue;
    }

    std::vector<std::string> got;
    bool errored = false;
    try
    {
      for (auto& v : jk::action::process_sequence(in, prog->current_seq))
        got.push_back(jk::to_json(v.get()));
    }
    catch (...)
    {
      errored = true;
    }

    // Compare against jq's expected lines. jq writes them as JSON, but with
    // its own key order; compare the parsed forms so ordering does not count.
    bool same = !errored && got.size() == g.expected.size();
    if (same)
    {
      for (std::size_t i = 0; i < got.size(); i++)
      {
        try
        {
          if (jk::to_json(parse_json(g.expected[i])) != got[i])
          {
            same = false;
            break;
          }
        }
        catch (...)
        {
          same = false;
          break;
        }
      }
    }

    if (same)
    {
      ++pass;
    }
    else
    {
      ++mismatch;
      std::printf("MISMATCH  %s\n", g.program.c_str());
      std::printf("   input:    %s\n", g.input.c_str());
      std::printf("   expected:");
      for (const auto& e : g.expected)
        std::printf(" %s", e.c_str());
      std::printf("\n   got:     ");
      if (errored)
        std::printf(" <error>");
      for (const auto& e : got)
        std::printf(" %s", e.c_str());
      std::printf("\n");
    }
  }

  const int total = pass + mismatch + unsupported + bad_input + rejected_ok;
  std::printf(
      "\n%d group(s): %d pass, %d mismatch, %d unsupported (not implemented), "
      "%d correctly rejected, %d unreadable input\n",
      total,
      pass,
      mismatch,
      unsupported,
      rejected_ok,
      bad_input);
  return mismatch != 0;
}
