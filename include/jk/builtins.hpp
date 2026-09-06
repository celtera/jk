#pragma once
#include <jk/actions.hpp>
#include <jk/generator.hpp>
#include <jk/ops.hpp>
#include <jk/print.hpp>
#include <jk/value.hpp>

#include <algorithm>

/**
 * \file builtins.hpp
 *
 * The named functions and the operators, as actions.
 *
 * Everything here is shaped the same way: an action takes the input value and
 * yields zero or more results. Operators combine two sub-actions, which is why
 * they cannot be plain value-to-value functions - either side may itself
 * produce several values, and jq then takes every combination.
 */
namespace jk::action
{

//! Yield a fixed value, ignoring the input. What a literal compiles to.
inline generator<value> constant(const value& v)
{
  co_yield v;
}

/**
 * @brief Combine two sub-expressions with a binary operator.
 *
 * jq iterates the *right* operand in the outer loop: `(1,2) + (10,20)` gives
 * 11, 12, 21, 22 rather than 11, 21, 12, 22. Verified against jq 1.8.
 */
template <typename Op>
generator<value>
binary(const value& in, const action_fun& lhs, const action_fun& rhs, Op op)
{
  for (auto& r : rhs(in))
    for (auto& l : lhs(in))
      co_yield op(l.get(), r.get());
}

//! Comparison operators, which are the same shape as arithmetic but fold the
//! three-way result down to a boolean.
template <typename Pred>
generator<value> comparison(
    const value& in,
    const action_fun& lhs,
    const action_fun& rhs,
    Pred pred)
{
  for (auto& r : rhs(in))
    for (auto& l : lhs(in))
      co_yield value{bool(pred(compare(l.get(), r.get())))};
}

//! Equality is distinct from ordering: NaN sorts but never equals itself.
inline generator<value> equality(
    const value& in,
    const action_fun& lhs,
    const action_fun& rhs,
    bool negate = false)
{
  for (auto& r : rhs(in))
    for (auto& l : lhs(in))
      co_yield value{equal(l.get(), r.get()) != negate};
}

//! `and` / `or`. Short-circuits per left-hand value, as jq does: the right
//! side is not evaluated when the left already decides the result.
inline generator<value>
logical_and(const value& in, const action_fun& lhs, const action_fun& rhs)
{
  for (auto& l : lhs(in))
  {
    if (!truthy(l.get()))
      co_yield value{false};
    else
      for (auto& r : rhs(in))
        co_yield value{truthy(r.get())};
  }
}

inline generator<value>
logical_or(const value& in, const action_fun& lhs, const action_fun& rhs)
{
  for (auto& l : lhs(in))
  {
    if (truthy(l.get()))
      co_yield value{true};
    else
      for (auto& r : rhs(in))
        co_yield value{truthy(r.get())};
  }
}

/**
 * @brief `a // b`.
 *
 * Yields every truthy output of the left. Only if it finishes without one
 * does the right run. Errors propagate, including after preceding outputs.
 */
inline generator<value>
alternative(const value& in, const action_fun& lhs, const action_fun& rhs)
{
  bool produced = false;
  for (auto& l : lhs(in))
    if (truthy(l.get()))
    {
      produced = true;
      co_yield l;
    }

  if (!produced)
    for (auto& r : rhs(in))
      co_yield r;
}

//! `expr?`: stop at an error instead of propagating it. Whatever was produced
//! before the error is kept, as in jq.
inline generator<value> optional(const value& in, const action_fun& act)
{
  try
  {
    for (auto& v : act(in))
      co_yield v;
  }
  catch (const error&)
  {
  }
}

inline generator<value> conditional(
    const value& in,
    const action_fun& cond,
    const action_fun& yes,
    const action_fun& no)
{
  for (auto& result : cond(in))
    for (auto& output : (truthy(result.get()) ? yes : no)(in))
      co_yield output;
}

inline generator<value>
try_catch(const value& in, const action_fun& body, const action_fun& handler)
{
  // C++ forbids co_yield inside a catch handler.
  value payload;
  bool failed = false;
  try
  {
    for (auto& output : body(in))
      co_yield output;
  }
  catch (const error& e)
  {
    payload = e.payload;
    failed = true;
  }
  if (failed)
    for (auto& output : handler(payload))
      co_yield output;
}
//! `select(f)`: pass the input through when f says so.
inline generator<value> select(const value& in, const action_fun& pred)
{
  for (auto& p : pred(in))
    if (truthy(p.get()))
      co_yield in;
}

//! Unary minus.
inline generator<value> negate(const value& in, const action_fun& act)
{
  for (auto& v : act(in))
  {
    if (!is_number(v.get()))
      throw error{
          std::string{type_name(v.get())} + " (" + brief(v.get())
          + ") cannot be negated"};
    if (const auto* integer = get_if<int64_t>(&v.get().v);
        integer && *integer != std::numeric_limits<int64_t>::min())
      co_yield value{-*integer};
    else
      co_yield number(-as_number(v.get()));
  }
}

// ---------------------------------------------------------------- builtins

inline generator<value> b_length(const value& in)
{
  switch (kind_of(in))
  {
    case kind::null:
      co_yield value{int64_t(0)};
      break;
    case kind::boolean:
      throw error{"boolean (" + brief(in) + ") has no length"};
    case kind::number:
      co_yield number(std::abs(as_number(in)));
      break;
    case kind::string:
    {
      // jq counts codepoints, not bytes: a two-byte UTF-8 char is length 1.
      const auto& s = *get_if<string_type>(&in.v);
      int64_t n = 0;
      for (unsigned char c : s)
        if ((c & 0xC0) != 0x80)
          ++n;
      co_yield value{n};
      break;
    }
    case kind::array:
      co_yield value{int64_t(get_if<list_type>(&in.v)->size())};
      break;
    default:
      co_yield value{int64_t(get_if<map_type>(&in.v)->size())};
      break;
  }
}

inline generator<value> b_type(const value& in)
{
  co_yield value{string_type{type_name(in)}};
}

inline generator<value> b_not(const value& in)
{
  co_yield value{!truthy(in)};
}

inline generator<value> b_keys(const value& in)
{
  list_type r;
  if (auto m = get_if<map_type>(&in.v))
  {
    // The map is already ordered by key, which is the order jq returns.
    for (const auto& [k, v] : *m)
      r.push_back(value{k});
  }
  else if (auto l = get_if<list_type>(&in.v))
  {
    for (std::size_t i = 0; i < l->size(); i++)
      r.push_back(value{int64_t(i)});
  }
  else
  {
    throw error{
        std::string{type_name(in)} + " (" + brief(in) + ") has no keys"};
  }
  co_yield value{std::move(r)};
}

//! `values` is jq's `select(. != null)` - it passes the input through unless
//! it is null. It does *not* extract an object's values; that is `.[]`.
inline generator<value> b_values(const value& in)
{
  if (!get_if<null_t>(&in.v))
    co_yield in;
}

//! `to_entries`: {"a":1} -> [{"key":"a","value":1}]
inline generator<value> b_to_entries(const value& in)
{
  list_type r;
  if (auto m = get_if<map_type>(&in.v))
  {
    r.reserve(m->size());
    for (const auto& [k, v] : *m)
    {
      map_type e;
      e["key"] = value{k};
      e["value"] = v;
      r.push_back(value{std::move(e)});
    }
  }
  else if (auto l = get_if<list_type>(&in.v))
  {
    r.reserve(l->size());
    for (std::size_t i = 0; i < l->size(); ++i)
    {
      map_type e;
      e["key"] = value{int64_t(i)};
      e["value"] = (*l)[i];
      r.push_back(value{std::move(e)});
    }
  }
  else
    throw error{
        std::string{type_name(in)} + " (" + brief(in) + ") has no keys"};
  co_yield value{std::move(r)};
}

inline generator<value> b_add(const value& in)
{
  auto l = get_if<list_type>(&in.v);
  if (!l)
    throw error{"Cannot iterate over " + std::string{type_name(in)}};
  value acc{null_t{}};
  for (const auto& e : *l)
    acc = add(acc, e);
  co_yield std::move(acc);
}

inline generator<value> b_min(const value& in)
{
  auto l = get_if<list_type>(&in.v);
  if (!l)
    throw error{"Cannot iterate over " + std::string{type_name(in)}};
  if (l->empty())
  {
    co_yield value{null_t{}};
  }
  else
  {
    const value* best = &(*l)[0];
    for (const auto& e : *l)
      if (compare(e, *best) < 0)
        best = &e;
    co_yield *best;
  }
}

inline generator<value> b_max(const value& in)
{
  auto l = get_if<list_type>(&in.v);
  if (!l)
    throw error{"Cannot iterate over " + std::string{type_name(in)}};
  if (l->empty())
  {
    co_yield value{null_t{}};
  }
  else
  {
    // Last of equals, as jq does.
    const value* best = &(*l)[0];
    for (const auto& e : *l)
      if (compare(e, *best) >= 0)
        best = &e;
    co_yield *best;
  }
}

//! Preserve equal-key order without std::stable_sort's hidden heap buffer.
template <typename Compare>
inline config::vector<std::size_t>
stable_indices(std::size_t size, Compare compare)
{
  config::vector<std::size_t> indices;
  indices.reserve(size);
  for (std::size_t i = 0; i < size; ++i)
    indices.push_back(i);
  std::sort(
      indices.begin(),
      indices.end(),
      [&](auto a, auto b)
      {
        const int order = compare(a, b);
        return order < 0 || (order == 0 && a < b);
      });
  return indices;
}

inline list_type sorted_values(const list_type& input)
{
  auto order = stable_indices(
      input.size(),
      [&](auto a, auto b) { return compare(input[a], input[b]); });
  list_type output;
  output.reserve(input.size());
  for (auto index : order)
    output.push_back(input[index]);
  return output;
}

inline generator<value> b_sort(const value& in)
{
  auto l = get_if<list_type>(&in.v);
  if (!l)
    throw error{
        std::string{type_name(in)} + " (" + brief(in)
        + ") cannot be sorted, as it is not an array"};
  auto r = sorted_values(*l);
  co_yield value{std::move(r)};
}

inline generator<value> b_unique(const value& in)
{
  auto l = get_if<list_type>(&in.v);
  if (!l)
    throw error{
        std::string{type_name(in)} + " (" + brief(in)
        + ") cannot be sorted, as it is not an array"};
  auto r = sorted_values(*l);
  r.erase(
      std::unique(
          r.begin(),
          r.end(),
          [](const value& a, const value& b) { return compare(a, b) == 0; }),
      r.end());
  co_yield value{std::move(r)};
}

inline generator<value> b_reverse(const value& in)
{
  if (auto l = get_if<list_type>(&in.v))
  {
    list_type r(l->rbegin(), l->rend());
    co_yield value{std::move(r)};
  }
  else if (auto s = get_if<string_type>(&in.v))
  {
    co_yield value{string_type{s->rbegin(), s->rend()}};
  }
  else if (get_if<null_t>(&in.v))
  {
    co_yield value{list_type{}};
  }
  else
  {
    throw error{"Cannot reverse " + std::string{type_name(in)}};
  }
}

//! Flatten without recursing: the nesting depth comes from outside, and a
//! recursive walk of it would put the execution thread's stack at the mercy
//! of whatever sent the message.
inline void flatten_into(const list_type& src, list_type& dst)
{
  struct frame
  {
    const list_type* list;
    std::size_t idx;
  };
  config::vector<frame> stack;
  stack.push_back({&src, 0});

  while (!stack.empty())
  {
    auto& top = stack.back();
    if (top.idx >= top.list->size())
    {
      stack.pop_back();
      continue;
    }
    const value& e = (*top.list)[top.idx++];
    if (auto sub = get_if<list_type>(&e.v))
      stack.push_back({sub, 0});
    else
      dst.push_back(e);
  }
}

inline generator<value> b_flatten(const value& in)
{
  auto l = get_if<list_type>(&in.v);
  if (!l)
    throw error{"Cannot flatten " + std::string{type_name(in)}};
  list_type r;
  flatten_into(*l, r);
  co_yield value{std::move(r)};
}

//! The type filters: each passes the input through only if it has that type.
//! `[ .. | numbers ]` pulling every number out of a nested reply is the
//! reason these are worth having.
template <kind K>
generator<value> b_of_kind(const value& in)
{
  if (kind_of(in) == K)
    co_yield in;
}

inline generator<value> b_scalars(const value& in)
{
  const auto k = kind_of(in);
  if (k != kind::array && k != kind::object)
    co_yield in;
}

inline generator<value> b_iterables(const value& in)
{
  const auto k = kind_of(in);
  if (k == kind::array || k == kind::object)
    co_yield in;
}

//! `empty`: the action that produces nothing.
inline generator<value> b_empty(const value&)
{
  co_return;
}

inline generator<value> b_tostring(const value& in)
{
  if (get_if<string_type>(&in.v))
  {
    co_yield in;
  }
  else
  {
    string_type out;
    render_json(in, out);
    co_yield value{std::move(out)};
  }
}

inline generator<value> b_tonumber(const value& in)
{
  if (is_number(in))
  {
    co_yield in;
  }
  else if (auto s = get_if<string_type>(&in.v))
  {
    const auto d = parse_number(*s);
    if (!d)
      throw error{"Cannot parse '" + std::string{*s} + "' as number"};
    co_yield number(*d);
  }
  else
  {
    throw error{
        std::string{type_name(in)} + " (" + brief(in)
        + ") cannot be parsed as a number"};
  }
}

template <typename F>
generator<value> numeric1(const value& in, F f, const char* name)
{
  if (!is_number(in))
    throw error{
        std::string{type_name(in)} + " (" + brief(in) + ") number required ("
        + name + ")"};
  co_yield number(f(as_number(in)));
}

// ------------------------------------------------- builtins with an argument

//! `map(f)`: [ .[] | f ]
inline generator<value> b_map(const value& in, const action_fun& f)
{
  list_type r;
  for (auto& e : iterate_array(in))
    for (auto& v : f(e.get()))
      r.push_back(v.take());
  co_yield value{std::move(r)};
}

//! Key used to order by: jq takes *all* the outputs of f as the key, so
//! sort_by(.a, .b) orders by the pair.
inline value sort_key(const value& e, const action_fun& f)
{
  list_type k;
  for (auto& v : f(e))
    k.push_back(v.take());
  return value{std::move(k)};
}

inline generator<value> b_sort_by(const value& in, const action_fun& f)
{
  auto l = get_if<list_type>(&in.v);
  if (!l)
    throw error{
        std::string{type_name(in)} + " (" + brief(in)
        + ") cannot be sorted, as it is not an array"};

  list_type keys;
  keys.reserve(l->size());
  for (const auto& element : *l)
    keys.push_back(sort_key(element, f));
  auto order = stable_indices(
      keys.size(), [&](auto a, auto b) { return compare(keys[a], keys[b]); });

  list_type r;
  r.reserve(order.size());
  for (auto index : order)
    r.push_back((*l)[index]);
  co_yield value{std::move(r)};
}

inline generator<value> b_min_by(const value& in, const action_fun& f)
{
  auto l = get_if<list_type>(&in.v);
  if (!l)
    throw error{"Cannot iterate over " + std::string{type_name(in)}};
  if (l->empty())
  {
    co_yield value{null_t{}};
  }
  else
  {
    const value* best = &(*l)[0];
    value bestk = sort_key(*best, f);
    for (const auto& e : *l)
    {
      value k = sort_key(e, f);
      if (compare(k, bestk) < 0)
      {
        bestk = std::move(k);
        best = &e;
      }
    }
    co_yield *best;
  }
}

inline generator<value> b_max_by(const value& in, const action_fun& f)
{
  auto l = get_if<list_type>(&in.v);
  if (!l)
    throw error{"Cannot iterate over " + std::string{type_name(in)}};
  if (l->empty())
  {
    co_yield value{null_t{}};
  }
  else
  {
    const value* best = &(*l)[0];
    value bestk = sort_key(*best, f);
    for (const auto& e : *l)
    {
      value k = sort_key(e, f);
      if (compare(k, bestk) >= 0)
      {
        bestk = std::move(k);
        best = &e;
      }
    }
    co_yield *best;
  }
}

inline generator<value> b_group_by(const value& in, const action_fun& f)
{
  auto l = get_if<list_type>(&in.v);
  if (!l)
    throw error{
        std::string{type_name(in)} + " (" + brief(in)
        + ") cannot be grouped, as it is not an array"};

  list_type keys;
  keys.reserve(l->size());
  for (const auto& element : *l)
    keys.push_back(sort_key(element, f));
  auto order = stable_indices(
      keys.size(), [&](auto a, auto b) { return compare(keys[a], keys[b]); });

  list_type groups;
  for (std::size_t i = 0; i < order.size();)
  {
    list_type g;
    std::size_t j = i;
    while (j < order.size() && compare(keys[order[j]], keys[order[i]]) == 0)
      g.push_back((*l)[order[j++]]);
    groups.push_back(value{std::move(g)});
    i = j;
  }
  co_yield value{std::move(groups)};
}

inline generator<value> b_has(const value& in, const action_fun& f)
{
  for (auto& k : f(in))
  {
    if (auto m = get_if<map_type>(&in.v))
    {
      auto s = get_if<string_type>(&k.get().v);
      if (!s)
        throw error{
            "Cannot check whether object has a key of type "
            + std::string{type_name(k.get())}};
      co_yield value{m->find(*s) != m->end()};
    }
    else if (auto l = get_if<list_type>(&in.v))
    {
      if (!is_number(k.get()))
        throw error{
            "Cannot check whether array has a key of type "
            + std::string{type_name(k.get())}};
      const double i = as_number(k.get());
      co_yield value{i >= 0 && i < double(l->size())};
    }
    else
    {
      throw error{
          "Cannot check whether " + std::string{type_name(in)} + " has a key"};
    }
  }
}

//! Fold the condition stream without advancing either stream after a decision.
inline generator<value> b_quantify(
    const value& in,
    const action_fun& source,
    const action_fun& condition,
    bool all)
{
  for (auto& item : source(in))
    for (auto& result : condition(item.get()))
      if (truthy(result.get()) != all)
      {
        co_yield value{!all};
        co_return;
      }
  co_yield value{all};
}

inline generator<value> b_error(const value& in)
{
  throw error{in};
  co_return;
}

inline generator<value>
b_error_message(const value& in, const action_fun& message)
{
  for (auto& payload : message(in))
    throw error{payload.take()};
  co_return;
}

inline generator<value> b_first(const value& in, const action_fun& source)
{
  for (auto& output : source(in))
  {
    co_yield output;
    co_return;
  }
}

inline generator<value> b_last(const value& in, const action_fun& source)
{
  value last{null_t{}};
  bool produced = false;
  for (auto& output : source(in))
  {
    last = output.take();
    produced = true;
  }
  if (produced)
    co_yield std::move(last);
}

inline generator<value> b_isempty(const value& in, const action_fun& source)
{
  auto stream = source(in);
  co_yield value{stream.begin() == stream.end()};
}

inline generator<value>
b_limit(const value& in, const action_fun& count, const action_fun& source)
{
  const value zero{int64_t(0)};
  const value one{int64_t(1)};
  for (auto& n : count(in))
  {
    if (equal(n.get(), zero))
      continue;
    if (compare(n.get(), zero) <= 0)
      throw error{"limit doesn't support negative count"};
    value remaining = n.take();
    for (auto& output : source(in))
    {
      remaining = subtract(remaining, one);
      co_yield output;
      if (compare(remaining, zero) <= 0)
        break;
    }
  }
}

inline generator<value>
b_skip(const value& in, const action_fun& count, const action_fun& source)
{
  const value zero{int64_t(0)};
  const value one{int64_t(1)};
  for (auto& n : count(in))
  {
    if (compare(n.get(), zero) < 0)
      throw error{"skip doesn't support negative count"};
    value remaining = n.get();
    for (auto& output : source(in))
    {
      if (equal(n.get(), zero))
        co_yield output;
      else
      {
        remaining = subtract(remaining, one);
        if (compare(remaining, zero) < 0)
          co_yield output;
      }
    }
  }
}

inline generator<value>
b_nth(const value& in, const action_fun& count, const action_fun& source)
{
  const value zero{int64_t(0)};
  const value one{int64_t(1)};
  for (auto& n : count(in))
  {
    if (compare(n.get(), zero) < 0)
      throw error{"nth doesn't support negative indices"};
    value remaining = n.get();
    for (auto& output : source(in))
    {
      if (!equal(n.get(), zero))
        remaining = subtract(remaining, one);
      if (equal(n.get(), zero) || compare(remaining, zero) < 0)
      {
        co_yield output;
        break;
      }
    }
  }
}

inline generator<value>
b_range(const value& in, const action_fun& from, const action_fun& upto)
{
  for (auto& start : from(in))
    for (auto& stop : upto(in))
    {
      if (!is_number(start.get()) || !is_number(stop.get()))
        throw error{"Range bounds must be numeric"};
      const double end = as_number(stop.get());
      for (double current = as_number(start.get()); !(current >= end);
           current += 1.)
        co_yield number(current);
    }
}

inline generator<value> b_range_step(
    const value& in,
    const action_fun& from,
    const action_fun& upto,
    const action_fun& step)
{
  const value zero{int64_t(0)};
  for (auto& start : from(in))
    for (auto& stop : upto(in))
      for (auto& increment : step(in))
      {
        const int direction = compare(increment.get(), zero);
        if (direction == 0)
          continue;
        value current = start.get();
        while (direction > 0 ? compare(current, stop.get()) < 0
                             : compare(current, stop.get()) > 0)
        {
          co_yield current;
          current = add(current, increment.get());
        }
      }
}

inline generator<value> b_join(const value& in, const action_fun& f)
{
  auto l = get_if<list_type>(&in.v);
  if (!l)
    throw error{"Cannot iterate over " + std::string{type_name(in)}};

  for (auto& sepv : f(in))
  {
    auto sep = get_if<string_type>(&sepv.get().v);
    if (!sep)
      throw error{
          std::string{type_name(sepv.get())}
          + " cannot be used as a separator"};

    string_type out;
    bool first = true;
    for (const auto& e : *l)
    {
      if (!first)
        out += *sep;
      first = false;
      // null becomes the empty string; other non-strings are rendered.
      switch (kind_of(e))
      {
        case kind::null:
          break;
        case kind::string:
          out += *get_if<string_type>(&e.v);
          break;
        case kind::array:
        case kind::object:
          throw error{"Cannot join with " + std::string{type_name(e)}};
        default:
        {
          render_json(e, out);
          break;
        }
      }
    }
    co_yield value{std::move(out)};
  }
}

inline generator<value> b_split(const value& in, const action_fun& f)
{
  if (!get_if<string_type>(&in.v))
    throw error{"split input must be a string"};
  for (auto& sep : f(in))
    co_yield divide(in, sep.get());
}

}

namespace jk::action
{
//! Resolve a builtin by name and arity; argument filters retain their input.
inline action_fun
make_builtin(std::string_view name, std::vector<action_fun> args)
{
  const auto arity = args.size();
  const auto wrap0 = [&](auto fn)
  { return action_fun{[fn](const value& in) { return fn(in); }}; };
  const auto wrap1 = [&](auto fn)
  {
    return action_fun{[fn, arg = std::move(args[0])](const value& in)
                      { return fn(in, arg); }};
  };
  const auto wrap2 = [&](auto fn)
  {
    return action_fun{[fn, a = std::move(args[0]), b = std::move(args[1])](
                          const value& in) { return fn(in, a, b); }};
  };
  const auto identity = [] { return action_fun{copy_all}; };
  const auto iterate = [] { return action_fun{iterate_array}; };
  if ((name == "any" || name == "all") && arity <= 2)
  {
    action_fun source = arity == 2 ? std::move(args[0]) : iterate();
    action_fun condition
        = arity == 0 ? identity() : std::move(args[arity - 1]);
    return action_fun{[source = std::move(source),
                       condition = std::move(condition),
                       all = name == "all"](const value& in)
                      { return b_quantify(in, source, condition, all); }};
  }
  if (name == "range" && arity >= 1 && arity <= 3)
  {
    action_fun from = arity == 1
                          ? action_fun{[zero = value{int64_t(0)}](const value&)
                                       { return constant(zero); }}
                          : std::move(args[0]);
    action_fun upto = std::move(args[arity == 1 ? 0 : 1]);
    if (arity == 3)
      return action_fun{[from = std::move(from),
                         upto = std::move(upto),
                         step = std::move(args[2])](const value& in)
                        { return b_range_step(in, from, upto, step); }};
    return action_fun{
        [from = std::move(from), upto = std::move(upto)](const value& in)
        { return b_range(in, from, upto); }};
  }
  if (arity == 0)
  {
    const auto num = [&](double (*fn)(double), const char* n)
    {
      return action_fun{[fn, n](const value& in)
                        { return numeric1(in, fn, n); }};
    };
    if (name == "length")
      return wrap0(b_length);
    if (name == "type")
      return wrap0(b_type);
    if (name == "not")
      return wrap0(b_not);
    if (name == "keys")
      return wrap0(b_keys);
    if (name == "keys_unsorted")
      return wrap0(b_keys);
    if (name == "values")
      return wrap0(b_values);
    if (name == "to_entries")
      return wrap0(b_to_entries);
    if (name == "add")
      return wrap0(b_add);
    if (name == "min")
      return wrap0(b_min);
    if (name == "max")
      return wrap0(b_max);
    if (name == "sort")
      return wrap0(b_sort);
    if (name == "unique")
      return wrap0(b_unique);
    if (name == "reverse")
      return wrap0(b_reverse);
    if (name == "flatten")
      return wrap0(b_flatten);
    if (name == "empty")
      return wrap0(b_empty);
    if (name == "error")
      return wrap0(b_error);
    if (name == "nulls")
      return wrap0(b_of_kind<kind::null>);
    if (name == "booleans")
      return wrap0(b_of_kind<kind::boolean>);
    if (name == "numbers")
      return wrap0(b_of_kind<kind::number>);
    if (name == "strings")
      return wrap0(b_of_kind<kind::string>);
    if (name == "arrays")
      return wrap0(b_of_kind<kind::array>);
    if (name == "objects")
      return wrap0(b_of_kind<kind::object>);
    if (name == "scalars")
      return wrap0(b_scalars);
    if (name == "iterables")
      return wrap0(b_iterables);
    if (name == "tostring")
      return wrap0(b_tostring);
    if (name == "tonumber")
      return wrap0(b_tonumber);
    if (name == "first")
      return action_fun{[](const value& in) { return access_array(0, in); }};
    if (name == "last")
      return action_fun{[](const value& in) { return access_array(-1, in); }};
    if (name == "floor")
      return num([](double d) { return std::floor(d); }, "floor");
    if (name == "ceil")
      return num([](double d) { return std::ceil(d); }, "ceil");
    if (name == "round")
      return num([](double d) { return std::round(d); }, "round");
    if (name == "fabs")
      return num([](double d) { return std::abs(d); }, "fabs");
    if (name == "sqrt")
      return num([](double d) { return std::sqrt(d); }, "sqrt");
  }
  else if (arity == 1)
  {
    if (name == "map")
      return wrap1(b_map);
    if (name == "select")
      return wrap1(select);
    if (name == "sort_by")
      return wrap1(b_sort_by);
    if (name == "min_by")
      return wrap1(b_min_by);
    if (name == "max_by")
      return wrap1(b_max_by);
    if (name == "group_by")
      return wrap1(b_group_by);
    if (name == "has")
      return wrap1(b_has);
    if (name == "join")
      return wrap1(b_join);
    if (name == "split")
      return wrap1(b_split);
    if (name == "error")
      return wrap1(b_error_message);
    if (name == "first")
      return wrap1(b_first);
    if (name == "last")
      return wrap1(b_last);
    if (name == "isempty")
      return wrap1(b_isempty);
    if (name == "nth")
      return action_fun{
          [base = identity(), key = std::move(args[0])](const value& in)
          { return index_by(in, base, key); }};
  }
  else if (arity == 2)
  {
    if (name == "nth")
      return wrap2(b_nth);
    if (name == "limit")
      return wrap2(b_limit);
    if (name == "skip")
      return wrap2(b_skip);
  }
  throw error{
      std::string{name} + "/" + std::to_string(arity) + " is not defined"};
}

} // namespace jk::action
