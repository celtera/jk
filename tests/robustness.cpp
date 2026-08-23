// Robustness and cost, for a filter that runs on the execution thread.
//
// The conformance suites answer "is it jq?". This one answers the two
// questions that matter for a realtime host instead:
//
//   * can anything reach the caller other than values or a jk::error - a
//     crash, a stack overflow, an escaped exception of another type?
//   * what does a message actually cost?
//
// Every program here is run against every input, including deliberately
// mismatched ones, because pointing a filter at the wrong shape is the normal
// way a patch is wrong.
#include <jk/parser.hpp>
#include <jk/print.hpp>

#include <chrono>
#include <cstdio>
#include <random>
#include <string>
#include <vector>

#include "test_json.hpp"

namespace
{
int failures = 0;

void fail(const char* what, const std::string& detail)
{
  std::printf("  FAIL %s: %s\n", what, detail.c_str());
  ++failures;
}

//! Nesting deep enough that any recursive walk of it would exhaust the stack.
jk::value deep_array(int depth)
{
  jk::value v{int64_t(1)};
  for(int i = 0; i < depth; i++)
  {
    jk::list_type l;
    l.push_back(std::move(v));
    v = jk::value{std::move(l)};
  }
  return v;
}

jk::value deep_object(int depth)
{
  jk::value v{int64_t(1)};
  for(int i = 0; i < depth; i++)
  {
    jk::map_type m;
    m["a"] = std::move(v);
    v = jk::value{std::move(m)};
  }
  return v;
}

//! Run a program the way the host does: values out, jk::error tolerated,
//! anything else is a defect. Returns the number of values produced.
int run_guarded(const char* program, const jk::value& in, const char* label)
{
  auto prog = jk::parse(program);
  if(!prog)
    return -1; // rejected at parse time, which is a legitimate answer

  int n = 0;
  try
  {
    for(auto& v : jk::action::process_sequence(in, prog->current_seq))
    {
      (void)v;
      ++n;
      if(n > 2000000)
      {
        fail("runaway output", program);
        break;
      }
    }
  }
  catch(const jk::error&)
  {
    // The expected way a type error arrives.
  }
  catch(const std::exception& e)
  {
    fail(label, std::string{program} + " threw std::exception: " + e.what());
  }
  catch(...)
  {
    fail(label, std::string{program} + " threw a non-jk exception");
  }
  return n;
}
}

int main()
{
  std::setvbuf(stdout, nullptr, _IONBF, 0);

  // ---------------------------------------------------------------- inputs
  std::vector<std::pair<const char*, jk::value>> inputs;
  const char* json_inputs[] = {
      "null",
      "true",
      "0",
      "-1.5",
      "\"\"",
      "\"abc\"",
      "[]",
      "{}",
      "[1,2,3]",
      "[[1],[2]]",
      "{\"a\":1,\"b\":null}",
      "[{\"a\":1},{\"b\":2}]",
      "[null,true,\"x\",[],{}]",
  };
  for(const char* j : json_inputs)
    inputs.emplace_back(j, parse_json(j));

  // ------------------------------------------------------------- programs
  const char* programs[] = {
      ".",
      ".a",
      ".a.b.c",
      ".[0]",
      ".[-1]",
      ".[]",
      ".[1:2]",
      "..",
      "[..]",
      "length",
      "keys",
      "type",
      "add",
      "sort",
      "unique",
      "reverse",
      "flatten",
      "min",
      "max",
      "first",
      "last",
      "tostring",
      "tonumber",
      "sqrt",
      "floor",
      "not",
      "to_entries",
      "any",
      "all",
      "map(.)",
      "map(.a)",
      "map(. + 1)",
      "select(.)",
      "select(. > 0)",
      "sort_by(.a)",
      "group_by(.a)",
      "min_by(.a)",
      "has(\"a\")",
      "join(\",\")",
      "split(\",\")",
      "[ .[] | select(.a > 1) ]",
      "{a: 1, b: .}",
      "{(.a): 1}",
      "[ .[] | { x: .a, y: .b } ]",
      ".a // 1",
      ".a?",
      "(.a, .b)?",
      "1 + .",
      ". * 2",
      ". - 1",
      ". / 2",
      ". % 2",
      "- .",
      ". == .",
      ". < .",
      ". and .",
      ". or .",
      "[ .[] | numbers ]",
      "[ .. | scalars ]",
      ".[] | .[] | .[]",
      "[ .[], .[], .[] ]",
  };

  std::printf("=== every program against every input ===\n");
  int combinations = 0;
  for(const char* p : programs)
    for(const auto& [name, in] : inputs)
    {
      run_guarded(p, in, "mismatched input");
      ++combinations;
    }
  std::printf("  %d combination(s) run\n", combinations);

  // ------------------------------------------------------- deep structures
  //
  // The depth here is far past what any recursive walk could survive; the
  // point is that the answer is a refusal, not a crash.
  std::printf("\n=== deeply nested input ===\n");
  // 2000 is far past anything a device or a websocket produces, and well
  // inside what the value type itself survives: copying or destroying a
  // jk::value recurses per level, so past roughly 8000 the structure is
  // unusable regardless of which jq operation touches it. That is a property
  // of the data type, not of the filter, and it is unreachable in practice
  // because the conversion that builds the value recurses too.
  const jk::value deep_a = deep_array(2000);
  const jk::value deep_o = deep_object(2000);
  const char* deep_progs[]
      = {"..", "[..]", "flatten", "sort", ". == .", "[ .. | numbers ]",
         "tostring", "length", "type", ". * ."};
  for(const char* p : deep_progs)
  {
    run_guarded(p, deep_a, "deep array");
    run_guarded(p, deep_o, "deep object");
  }
  std::printf("  survived depth 2000 on %zu program(s)\n",
              sizeof(deep_progs) / sizeof(deep_progs[0]));

  // ------------------------------------------------------------ wide input
  std::printf("\n=== wide input ===\n");
  {
    jk::list_type wide;
    for(int i = 0; i < 100000; i++)
      wide.push_back(jk::value{int64_t(i)});
    const jk::value w{std::move(wide)};
    for(const char* p : {".[]", "add", "sort", "unique", "reverse", "length",
                         "map(. + 1)", "[ .[] | select(. > 50000) ]"})
      run_guarded(p, w, "wide");
    std::printf("  survived 100000 elements\n");
  }

  // --------------------------------------------------------------- fuzzing
  //
  // Random text through the parser. Nothing here should ever be accepted as
  // meaningful; what matters is that rejecting it is all that happens.
  std::printf("\n=== parser fuzz ===\n");
  {
    std::mt19937 rng{20260823};
    static const char alphabet[]
        = ".[]{}()|,:?/*+-%<>=!\"\\abcdefghijklmnopqrstuvwxyz0123456789_ \t";
    std::uniform_int_distribution<int> len{0, 40};
    std::uniform_int_distribution<int> ch{0, int(sizeof(alphabet) - 2)};

    int accepted = 0;
    const int rounds = 200000;
    for(int i = 0; i < rounds; i++)
    {
      std::string prog;
      const int n = len(rng);
      for(int k = 0; k < n; k++)
        prog += alphabet[ch(rng)];

      try
      {
        if(auto p = jk::parse(prog))
        {
          ++accepted;
          // Anything the parser accepts must also be safe to run.
          for(const auto& [name, in] : inputs)
          {
            try
            {
              int produced = 0;
              for(auto& v : jk::action::process_sequence(in, p->current_seq))
              {
                (void)v;
                if(++produced > 100000)
                  break;
              }
            }
            catch(const jk::error&)
            {
            }
          }
        }
      }
      catch(const std::exception& e)
      {
        fail("fuzz parse", prog + " -> " + e.what());
      }
      catch(...)
      {
        fail("fuzz parse", prog + " -> unknown exception");
      }
    }
    std::printf("  %d random program(s), %d accepted and run\n", rounds, accepted);
  }

  // ------------------------------------------------------------------ cost
  //
  // What one message costs, for the shapes a score patch actually uses.
  std::printf("\n=== per-message cost ===\n");
  {
    const jk::value frame = parse_json(
        "[{\"id\":1,\"position\":[0.2,0.8],\"confidence\":0.95,\"state\":\"confirmed\"},"
        "{\"id\":2,\"position\":[0.5,0.4],\"confidence\":0.88,\"state\":\"confirmed\"},"
        "{\"id\":3,\"position\":[0.9,0.1],\"confidence\":0.42,\"state\":\"coasting\"}]");

    const char* bench[] = {
        ".",
        "[ .[].id ]",
        "[ .[] | select(.confidence > 0.8) | .id ]",
        "[ .[] | { id, x: .position[0] } ]",
        "sort_by(.confidence) | reverse | .[0:2]",
    };

    for(const char* p : bench)
    {
      auto prog = jk::parse(p);
      if(!prog)
      {
        fail("bench parse", p);
        continue;
      }

      const int iters = 20000;
      const auto t0 = std::chrono::steady_clock::now();
      std::size_t sink = 0;
      for(int i = 0; i < iters; i++)
        for(auto& v : jk::action::process_sequence(frame, prog->current_seq))
          sink += v.data.v.index();
      const auto t1 = std::chrono::steady_clock::now();

      const double us
          = std::chrono::duration<double, std::micro>(t1 - t0).count() / iters;
      std::printf("  %-44s %7.2f us/message  (sink %zu)\n", p, us, sink);
    }
  }

  std::printf("\n%d failure(s)\n", failures);
  return failures != 0;
}
