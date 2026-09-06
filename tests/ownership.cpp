#include "test_json.hpp"

#include <catch2/catch_test_macros.hpp>
#include <jk/actions.hpp>
#include <jk/parser.hpp>
#include <jk/print.hpp>

#include <array>
#include <exception>
#include <optional>

TEST_CASE("projection streams input larger than its bounded working storage")
{
  jk::list_type objects;
  for (int i = 0; i < 64; ++i)
  {
    jk::map_type object;
    object.emplace("id", i);
    object.emplace("state", i % 2 ? "s1" : "s2");
    object.emplace("payload", jk::string_type(8192, 'x'));
    objects.emplace_back(std::move(object));
  }
  const jk::value input{std::move(objects)};
  auto program = jk::parse(".[] | select(.state == \"s2\") | .id");
  REQUIRE(program);
  std::array<std::byte, 256 * 1024> storage;
  jk::evaluation_context context{storage};
  for (int repetition = 0; repetition < 3; ++repetition)
  {
    jk::evaluation_scope evaluation{context};
    int expected = 0;
    for (auto& output :
         jk::action::process_sequence(input, program->current_seq))
    {
      REQUIRE(output.get() == jk::value{expected});
      expected += 2;
    }
    REQUIRE(expected == 64);
  }
  const auto& original
      = *jk::config::variant_ns::get_if<jk::list_type>(&input.v);
  REQUIRE(
      jk::config::variant_ns::get_if<jk::map_type>(&original.back().v)
          ->at("payload")
      == jk::value{jk::string_type(8192, 'x')});
}

TEST_CASE("persisted computed results survive producer and arena destruction")
{
  jk::value retained;
  {
    std::array<std::byte, 256 * 1024> storage;
    jk::evaluation_context context{storage};
    auto program = jk::parse(
        "{items: [range(0; 4)], text: (\"abcdefghijklmnopqrstuvwxyz\" + "
        "\"0123456789\")}");
    REQUIRE(program);
    const jk::value input;
    {
      jk::evaluation_scope evaluation{context};
      for (auto& output :
           jk::action::process_sequence(input, program->current_seq))
        retained = output.persist();
    }
    // Reuse the same arena before examining the escaped result.
    {
      jk::evaluation_scope evaluation{context};
      jk::string_type overwrite(32768, 'z');
      REQUIRE(overwrite.back() == 'z');
    }
  }
  REQUIRE(
      retained
      == parse_json(
          R"({"items":[0,1,2,3],"text":"abcdefghijklmnopqrstuvwxyz0123456789"})"));
}

TEST_CASE("nested evaluation keeps suspended outer producers alive")
{
  std::array<std::byte, 256 * 1024> storage;
  jk::evaluation_context context{storage};
  auto outer = jk::parse("[range(0; 4)]");
  auto inner = jk::parse("[range(8; 12)]");
  REQUIRE(outer);
  REQUIRE(inner);
  const jk::value input;
  jk::evaluation_scope evaluation{context};
  auto stream = jk::action::process_sequence(input, outer->current_seq);
  auto output = stream.begin();
  REQUIRE(output != stream.end());
  {
    jk::evaluation_scope nested{context};
    for (auto& result :
         jk::action::process_sequence(input, inner->current_seq))
      REQUIRE(jk::to_json(result.get()) == "[8,9,10,11]");
  }
  REQUIRE(jk::to_json((*output).get()) == "[0,1,2,3]");
  ++output;
  REQUIRE(output == stream.end());
}

TEST_CASE("program constants and errors do not escape with arena ownership")
{
  std::optional<jk::actions::handlers> compiled;
  std::exception_ptr failure;
  {
    std::array<std::byte, 256 * 1024> storage;
    jk::evaluation_context context{storage};
    const jk::value input;
    try
    {
      jk::evaluation_scope evaluation{context};
      compiled = jk::parse(R"("abcdefghijklmnopqrstuvwxyz0123456789")");
      auto throws = jk::parse(
          R"(error({items: [range(0; 4)], message: "abcdefghijklmnopqrstuvwxyz0123456789"}))");
      REQUIRE(throws);
      for (auto& result :
           jk::action::process_sequence(input, throws->current_seq))
        (void)result;
      FAIL("error must propagate");
    }
    catch (const jk::error&)
    {
      failure = std::current_exception();
    }
  }
  REQUIRE(compiled);
  const jk::value input;
  for (auto& result :
       jk::action::process_sequence(input, compiled->current_seq))
    REQUIRE(result.get() == jk::value{"abcdefghijklmnopqrstuvwxyz0123456789"});
  REQUIRE(failure);
  try
  {
    std::rethrow_exception(failure);
  }
  catch (const jk::error& error)
  {
    REQUIRE(
        error.payload
        == parse_json(
            R"({"items":[0,1,2,3],"message":"abcdefghijklmnopqrstuvwxyz0123456789"})"));
  }
}

TEST_CASE("strict storage rejects overflow and frames retain their allocator")
{
  std::array<std::byte, 16 * 1024> storage;
  jk::evaluation_context context{storage};
  {
    jk::evaluation_scope evaluation{context};
    REQUIRE_THROWS_AS(jk::string_type(1024 * 1024, 'x'), std::bad_alloc);
  }
  const jk::value input{42};
  jk::generator<jk::value> stream;
  {
    jk::allocation_scope selection{context.resource()};
    stream = jk::action::copy_all(input);
  }
  auto output = stream.begin();
  REQUIRE(output != stream.end());
  REQUIRE((*output).get() == input);
  // Destruction under normal heap selection must still free through context.
  stream = {};
  context.reset();
}
