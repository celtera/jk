#include <catch2/catch_all.hpp>
#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <jk/actions.hpp>
#include <jk/parser.hpp>
#include <jk/print.hpp>
#include <jk/value.hpp>
#include <string_view>
#include <vector>

void check_pattern(
    std::string_view pat,
    jk::value input,
    std::vector<jk::value> expected)
{
  INFO("Pattern: " << pat);
  INFO("Input: " << jk::to_string(input.v));
  for (auto& v : expected)
  {
    INFO("Expected: " << jk::to_string(v.v));
  }
  auto comp = jk::parse(pat);
  REQUIRE(comp);

  std::size_t k = 0;
  auto gen = jk::action::process_sequence(input, comp->current_seq);

  for (auto& v : gen)
  {
    REQUIRE(!v.data.v.valueless_by_exception());
    INFO("Got: " << to_string(v.data.v));

    REQUIRE(k < expected.size()); // matcher produces too much output
    INFO(" but expected " << to_string(expected[k].v));
    REQUIRE(expected[k] == v.data.v);
    k++;
  }
  REQUIRE(k == expected.size()); // matcher does not produce enough output
}

using jk::parse;
using L = jk::list_type;
using M = jk::map_type;
using V = jk::value;

TEST_CASE("parse_identity")
{
  check_pattern(". ", 123, {123});
  check_pattern(". ", 1.23, {1.23});
}

TEST_CASE("parse_iteration")
{
  check_pattern(
      ". [] ",
      L{1, 2.5, "foo"},
      {jk::value(1), jk::value(2.5), jk::value("foo")});
}

TEST_CASE("parse_iteration_map")
{
  check_pattern(
      ". [] ",
      M{{"a", 1}, {"b", 2.5}, {"c", "foo"}},
      {jk::value(1), jk::value(2.5), jk::value("foo")});
}

TEST_CASE("parse_access")
{
  check_pattern(". [0] ", L{1, 2.5, "foo"}, {1});
  check_pattern(". [1] ", L{1, 2.5, "foo"}, {2.5});
  check_pattern(". [1] ", L{"a", "b", "c"}, {"b"});
  check_pattern(". [2] ", L{1, 2.5, "foo"}, {"foo"});
  check_pattern(". [3] ", L{1, 2.5, "foo"}, {});
  check_pattern(". [-1] ", L{1, 2.5, "foo"}, {});
  check_pattern(
      ". [12] ", L{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13}, {12});

  check_pattern(". [1] ", L{L{"a"}, L{"b"}, L{"c"}}, L{V{L{"b"}}});
}

TEST_CASE("parse_access_child")
{
  check_pattern(
      ". [2][3] ", L{"foo", "bar", L{"a", "b", "c", "d", "e"}}, {"d"});
  check_pattern(
      ". [0] [2] ", L{L{"a", "b", "c", "d", "e"}, "foo", "bar"}, {"c"});
  check_pattern(". [0] [2] ", L{L{"a"}, "foo", "bar"}, {});
  check_pattern(". [0] [2] ", L{"foo", "bar", L{"a", "b", "c", "d", "e"}}, {});
}

TEST_CASE("sequence")
{
  check_pattern(". [1] | . ", L{"a", "b", "c", "d", "e"}, {"b"});

  check_pattern(". [1] | . [3] ", L{"a", L{0., 1., 2., 3., 4., 5.}}, {3.});

  check_pattern(
      ". [1] | . [] ",
      L{"a", L{0., 1., 2., 3., 4., 5.}, L{"a", "b", "c", "d", "e"}},
      {0., 1., 2., 3., 4., 5.});
}

TEST_CASE("iterate in sequence")
{
  check_pattern(
      ". [2] | . [] ",
      L{"a", L{0., 1., 2., 3., 4., 5.}, L{"a", "b", "c", "d", "e"}},
      {"a", "b", "c", "d", "e"});

  check_pattern(
      ". [] | . [2] ",
      L{"a", L{0., 1., 2., 3., 4., 5.}, L{"a", "b", "c", "d", "e"}},
      {2., "c"});

  check_pattern(
      ". [1] ",
      L{"a", L{L{0., 1.}, L{2., 3.}, L{4., 5.}}, L{"a", "b", "c", "d", "e"}},
      L{V{L{L{0., 1.}, L{2., 3.}, L{4., 5.}}}});
  check_pattern(
      ". [1] | . [2] ",
      L{"a", L{L{0., 1.}, L{2., 3.}, L{4., 5.}}, L{"a", "b", "c", "d", "e"}},
      L{V{L{4., 5.}}});
  check_pattern(
      ". [1] |.[2]",
      L{"a", L{L{0., 1.}, L{2., 3.}, L{4., 5.}}, L{"a", "b", "c", "d", "e"}},
      L{V{L{4., 5.}}});
  check_pattern(
      " .[1]|.[2]",
      L{"a", L{L{0., 1.}, L{2., 3.}, L{4., 5.}}, L{"a", "b", "c", "d", "e"}},
      L{V{L{4., 5.}}});
}

TEST_CASE("iterate map in sequence")
{
  check_pattern(". [] ", M{{"a", 123.}, {"b", 456.}}, {123., 456.});

  {
    M instance_map;
    instance_map["a"] = M{{"instance", 123.}};
    instance_map["b"] = M{{"instance", 456.}};
    check_pattern(". [] | .instance", instance_map, {123., 456.});
  }
}
TEST_CASE("identity in sequence")
{
  check_pattern(" . | . ", 123, {123});
  check_pattern(
      " . | . ", L{0., 1., 2., 3., 4., 5.}, L{V{L{0., 1., 2., 3., 4., 5.}}});
}

TEST_CASE("put child elements in list")
{
  check_pattern("[ . ] ", 123, L{V{L{123}}});

  check_pattern("[. [0] ]", L{L{1, 2, 3}, L{4, 5, 6}}, L{V{L{V{L{1, 2, 3}}}}});

  check_pattern("[ . ] | . ", 123, L{V{L{123}}});
  check_pattern(
      "[ . ] | . ",
      L{0., 1., 2., 3., 4., 5.},
      L{V{L{V{L{0., 1., 2., 3., 4., 5.}}}}});
}

TEST_CASE("put child elements in list twice")
{
  check_pattern("[ . ] | [ . ] ", 123, L{V{L{V{L{123}}}}});
  check_pattern(
      "[ . ] | [ . ] ",
      L{0., 1., 2., 3., 4., 5.},
      L{V{L{V{L{V{L{0., 1., 2., 3., 4., 5.}}}}}}});
}

TEST_CASE("put child elements in list rev")
{
  check_pattern(" . | [ . ] ", 123, L{V{L{123}}});
}

TEST_CASE("put child elements in list array")
{
  check_pattern("[. [0] | .[1]]", L{L{1, 2, 3}, L{4, 5, 6}}, L{V{L{2}}});

  // FIXME
  // check_pattern("[. [0] ] | [ .[1] ]", L{L{1, 2, 3}, L{4, 5, 6}}, {});
}

TEST_CASE("member access")
{
  check_pattern(".foo", M{{"a", 123}, {"foo", 456}, {"bar", 789}}, {456});
  check_pattern(
      ".foo.bar",
      M{{"a", 123}, {"foo", M{{"foo", 0.5}, {"bar", 1.5}}}, {"bar", 789}},
      {1.5});
  check_pattern(
      ".foo[0].bar",
      M{{"a", 123}, {"foo", L{M{{"foo", 0.5}, {"bar", 1.5}}}}, {"bar", 789}},
      {1.5});
  check_pattern(
      ".foo[0].bar[1][2]",
      M{{"a", 123},
        {"foo",
         L{M{{"foo", 0.5}, {"bar", L{L{1, 2, 3}, L{4, 5, 6}, L{7, 8, 9}}}}}},
        {"bar", 789}},
      {6});
  check_pattern(
      ".foo[0].bar[1][]",
      M{{"a", 123},
        {"foo",
         L{M{{"foo", 0.5}, {"bar", L{L{1, 2, 3}, L{4, 5, 6}, L{7, 8, 9}}}}}},
        {"bar", 789}},
      {4, 5, 6});
  check_pattern(
      ".foo[0].bar[][2]",
      M{{"a", 123},
        {"foo",
         L{M{{"foo", 0.5}, {"bar", L{L{1, 2, 3}, L{4, 5, 6}, L{7, 8, 9}}}}}},
        {"bar", 789}},
      {3, 6, 9});
}

TEST_CASE("member access with special chars")
{
    check_pattern(".foo_bar", M{{"a", 123}, {"foo_bar", 456}, {"bar", 789}}, {456});
    check_pattern(".\"foo_bar\"", M{{"a", 123}, {"foo_bar", 456}, {"bar", 789}}, {456});
    check_pattern(".\"foo.bar\"", M{{"a", 123}, {"foo.bar", 456}, {"bar", 789}}, {456});
    check_pattern(".\"foo bar\"", M{{"a", 123}, {"foo bar", 456}, {"bar", 789}}, {456});
}

TEST_CASE("comma sequence ")
{
  // FIXME check_pattern(".[1], .[3]", L{"a", "b", "c", "d", "e", "f"}, L{"b", "d"});
  check_pattern(
      "[ .[1], .[3] ]", L{"a", "b", "c", "d", "e", "f"}, L{V{L{"b", "d"}}});
}

TEST_CASE("range")
{
  check_pattern(".[0:2]", L{"a", "b", "c", "d", "e", "f"}, L{V{L{"a", "b"}}});

  check_pattern(
      "[ .[0:2] ] ", L{"a", "b", "c", "d", "e", "f"}, L{V{L{V{L{"a", "b"}}}}});

  check_pattern(
      ".[][0:2]",
      L{L{"a", "b", "c"}, L{"d", "e", "f"}},
      L{L{"a", "b"}, L{"d", "e"}});

  check_pattern(
      "[ .[][0:2] ] ",
      L{L{"a", "b", "c"}, L{"d", "e", "f"}},
      L{V{L{L{"a", "b"}, L{"d", "e"}}}});

  check_pattern(
      "[ .[][0:2][] ] ",
      L{L{"a", "b", "c"}, L{"d", "e", "f"}},
      L{V{L{"a", "b", "d", "e"}}});
}

TEST_CASE("comma")
{
  check_pattern(".[0, 2]", L{"a", "b", "c", "d", "e", "f"}, L{"a", "c"});
  check_pattern(
      "[ .[0, 2] ]", L{"a", "b", "c", "d", "e", "f"}, L{V{L{"a", "c"}}});
  check_pattern(
      "[ .[][1,2] ] ",
      L{L{"a", "b", "c"}, L{"d", "e", "f"}},
      L{V{L{"b", "c", "e", "f"}}});

  check_pattern(
      "[ .[][1,2] ] | .[0] ", L{L{"a", "b", "c"}, L{"d", "e", "f"}}, L{"b"});

  check_pattern("., .", 123, L{123, 123});

  check_pattern(
      ".[][0], .[][1]",
      L{L{1, 2, 3, 4}, L{"a", "b", "c", "d"}},
      L{1, "a", 2, "b"});
}

TEST_CASE("disjunct ranges")
{
  check_pattern(
      "[ .[][0] ]", L{L{1, 2, 3, 4}, L{"a", "b", "c", "d"}}, L{V{L{1, "a"}}});
  check_pattern(
      "[ .[][1] ]", L{L{1, 2, 3, 4}, L{"a", "b", "c", "d"}}, L{V{L{2, "b"}}});

  check_pattern(
      "[ [ .[][0] ] ]",
      L{L{1, 2, 3, 4}, L{"a", "b", "c", "d"}},
      L{V{L{V{L{1, "a"}}}}});
}

TEST_CASE("comma in array, map case")
{
  check_pattern(
      "[ .foo, .truc[1] ]",
      M{{"foo", 123}, {"bar", 456}, {"truc", L{"a", "b", "c"}}},
      L{V{L{V{123}, V{"b"}}}});
}

TEST_CASE("comma in array, range case")
{
  check_pattern(
      "[ .[][0] , .[][1] ]",
      L{L{1, 2, 3, 4}, L{"a", "b", "c", "d"}},
      L{V{L{1, "a", 2, "b"}}});
}
/*
TEST_CASE("comma in array nested")
{
  check_pattern(
      "[ [ .[0] ], [ .[1] ] ]",
      L{"a", "b", "c", "d"},
      L{V{L{L{"a"}, L{"b"}}}});

  check_pattern(
      "[ [ .[][0] ], [ .[][1] ] ]",
      L{L{1, 2, 3, 4}, L{"a", "b", "c", "d"}},
      L{L{1, "a"}, L{2, "b"}});
}
*/
TEST_CASE("create_object")
{
  check_pattern(" { foo: .[1] } ", L{10, 20, 30, 40}, L{M{{"foo", 20}}});
  check_pattern(
      " { foo: .[1], bar: [ .[2], .[3] ] } ",
      L{10, 20, 30, 40},
      L{M{{"foo", 20}, {"bar", L{30, 40}}}});
}

TEST_CASE("create_object_fast_syntax")
{
  check_pattern(
      " { foo, bar } ",
      M{{"x", 123}, {"foo", 456}, {"bar", 789}},
      L{M{{"foo", 456}, {"bar", 789}}});
}

TEST_CASE("recurse")
{
  check_pattern("..", jk::value("foo"), L{"foo"});
  check_pattern("..", L{"foo"}, L{L{"foo"}, "foo"});
  check_pattern("..", L{10, 20, 30, 40}, L{L{10, 20, 30, 40}, 10, 20, 30, 40});
  check_pattern(
      "..",
      L{L{1, 2}, L{"a", "b"}},
      L{L{L{1, 2}, L{"a", "b"}}, L{1, 2}, 1, 2, L{"a", "b"}, "a", "b"});

  check_pattern("..", M{{"a", "foo"}}, L{M{{"a", "foo"}}, "foo"});
}

TEST_CASE("recurse and get instance")
{
  M object{
      {"x", M{{"instance", 123}}},
      {"z", M{{"instance", 456}}},
      {"y", M{{"foo", 789}}},
  };

  check_pattern(".. | .instance", object, L{123, 456});
}

TEST_CASE("array range and iteration")
{
  M object{
      {"foo",
       L{
           M{{"instance", 0}},
           M{{"instance", 1}},
           M{{"instance", 2}},
           M{{"instance", 3}},
           M{{"instance", 4}},
           M{{"instance", 5}},
           M{{"instance", 6}},
       }}};

  check_pattern("[ .foo[2:5][].instance ]", object, L{V{L{2, 3, 4}}});
}
