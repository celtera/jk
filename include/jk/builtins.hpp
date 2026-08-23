#pragma once
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
inline generator<value> constant(value v)
{
  co_yield std::move(v);
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
  for(auto& r : rhs(in))
    for(auto& l : lhs(in))
      co_yield op(l.data, r.data);
}

//! Comparison operators, which are the same shape as arithmetic but fold the
//! three-way result down to a boolean.
template <typename Pred>
generator<value>
comparison(const value& in, const action_fun& lhs, const action_fun& rhs, Pred pred)
{
  for(auto& r : rhs(in))
    for(auto& l : lhs(in))
      co_yield value{bool(pred(compare(l.data, r.data)))};
}

//! `and` / `or`. Short-circuits per left-hand value, as jq does: the right
//! side is not evaluated when the left already decides the result.
inline generator<value>
logical_and(const value& in, const action_fun& lhs, const action_fun& rhs)
{
  for(auto& l : lhs(in))
  {
    if(!truthy(l.data))
      co_yield value{false};
    else
      for(auto& r : rhs(in))
        co_yield value{truthy(r.data)};
  }
}

inline generator<value>
logical_or(const value& in, const action_fun& lhs, const action_fun& rhs)
{
  for(auto& l : lhs(in))
  {
    if(truthy(l.data))
      co_yield value{true};
    else
      for(auto& r : rhs(in))
        co_yield value{truthy(r.data)};
  }
}

/**
 * @brief `a // b`.
 *
 * Yields every truthy output of the left. Only if it produced none - including
 * because it errored - does the right run. This is jq's defaulting idiom:
 * `.gain // 1.0`.
 *
 * Streamed, not collected: the common case is a single value, and buffering
 * would allocate on every message. The try has to enclose the loop rather than
 * each step, because a deferred error surfaces from begin() as readily as from
 * the increment.
 */
inline generator<value>
alternative(const value& in, const action_fun& lhs, const action_fun& rhs)
{
  bool produced = false;
  try
  {
    for(auto& l : lhs(in))
    {
      if(truthy(l.data))
      {
        produced = true;
        co_yield l.data;
      }
    }
  }
  catch(const error&)
  {
  }

  if(!produced)
    for(auto& r : rhs(in))
      co_yield std::move(r.data);
}

//! `expr?`: stop at an error instead of propagating it. Whatever was produced
//! before the error is kept, as in jq.
inline generator<value> optional(const value& in, const action_fun& act)
{
  try
  {
    for(auto& v : act(in))
      co_yield v.data;
  }
  catch(const error&)
  {
  }
}
//! `select(f)`: pass the input through when f says so.
inline generator<value> select(const value& in, const action_fun& pred)
{
  for(auto& p : pred(in))
    if(truthy(p.data))
      co_yield in;
}

//! Unary minus.
inline generator<value> negate(const value& in, const action_fun& act)
{
  for(auto& v : act(in))
  {
    if(!is_number(v.data))
      throw error{std::string{type_name(v.data)} + " (" + brief(v.data)
                  + ") cannot be negated"};
    co_yield number(-as_number(v.data));
  }
}

// ---------------------------------------------------------------- builtins

inline generator<value> b_length(const value& in)
{
  switch(kind_of(in))
  {
    case kind::null:
      co_yield value{int64_t(0)};
      break;
    case kind::boolean:
      throw error{"boolean (" + brief(in) + ") has no length"};
    case kind::number:
      co_yield number(std::abs(as_number(in)));
      break;
    case kind::string: {
      // jq counts codepoints, not bytes: a two-byte UTF-8 char is length 1.
      const auto& s = *get_if<string_type>(&in.v);
      int64_t n = 0;
      for(unsigned char c : s)
        if((c & 0xC0) != 0x80)
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
  if(auto m = get_if<map_type>(&in.v))
  {
    // The map is already ordered by key, which is the order jq returns.
    for(const auto& [k, v] : *m)
      r.push_back(value{k});
  }
  else if(auto l = get_if<list_type>(&in.v))
  {
    for(std::size_t i = 0; i < l->size(); i++)
      r.push_back(value{int64_t(i)});
  }
  else
  {
    throw error{std::string{type_name(in)} + " (" + brief(in) + ") has no keys"};
  }
  co_yield value{std::move(r)};
}

//! `values` is jq's `select(. != null)` - it passes the input through unless
//! it is null. It does *not* extract an object's values; that is `.[]`.
inline generator<value> b_values(const value& in)
{
  if(!get_if<null_t>(&in.v))
    co_yield in;
}

//! `to_entries`: {"a":1} -> [{"key":"a","value":1}]
inline generator<value> b_to_entries(const value& in)
{
  auto m = get_if<map_type>(&in.v);
  if(!m)
    throw error{std::string{type_name(in)} + " (" + brief(in)
                + ") has no keys"};
  list_type r;
  for(const auto& [k, v] : *m)
  {
    map_type e;
    e["key"] = value{k};
    e["value"] = v;
    r.push_back(value{std::move(e)});
  }
  co_yield value{std::move(r)};
}

inline generator<value> b_add(const value& in)
{
  auto l = get_if<list_type>(&in.v);
  if(!l)
    throw error{"Cannot iterate over " + std::string{type_name(in)}};
  value acc{null_t{}};
  for(const auto& e : *l)
    acc = add(acc, e);
  co_yield std::move(acc);
}

inline generator<value> b_min(const value& in)
{
  auto l = get_if<list_type>(&in.v);
  if(!l)
    throw error{"Cannot iterate over " + std::string{type_name(in)}};
  if(l->empty())
  {
    co_yield value{null_t{}};
  }
  else
  {
    const value* best = &(*l)[0];
    for(const auto& e : *l)
      if(compare(e, *best) < 0)
        best = &e;
    co_yield *best;
  }
}

inline generator<value> b_max(const value& in)
{
  auto l = get_if<list_type>(&in.v);
  if(!l)
    throw error{"Cannot iterate over " + std::string{type_name(in)}};
  if(l->empty())
  {
    co_yield value{null_t{}};
  }
  else
  {
    // Last of equals, as jq does.
    const value* best = &(*l)[0];
    for(const auto& e : *l)
      if(compare(e, *best) >= 0)
        best = &e;
    co_yield *best;
  }
}

inline generator<value> b_sort(const value& in)
{
  auto l = get_if<list_type>(&in.v);
  if(!l)
    throw error{std::string{type_name(in)} + " (" + brief(in) + ") cannot be sorted, as it is not an array"};
  list_type r = *l;
  std::stable_sort(r.begin(), r.end(), [](const value& a, const value& b) {
    return compare(a, b) < 0;
  });
  co_yield value{std::move(r)};
}

inline generator<value> b_unique(const value& in)
{
  auto l = get_if<list_type>(&in.v);
  if(!l)
    throw error{std::string{type_name(in)} + " (" + brief(in) + ") cannot be sorted, as it is not an array"};
  list_type r = *l;
  std::stable_sort(r.begin(), r.end(), [](const value& a, const value& b) {
    return compare(a, b) < 0;
  });
  r.erase(
      std::unique(
          r.begin(), r.end(),
          [](const value& a, const value& b) { return compare(a, b) == 0; }),
      r.end());
  co_yield value{std::move(r)};
}

inline generator<value> b_reverse(const value& in)
{
  if(auto l = get_if<list_type>(&in.v))
  {
    list_type r(l->rbegin(), l->rend());
    co_yield value{std::move(r)};
  }
  else if(auto s = get_if<string_type>(&in.v))
  {
    co_yield value{string_type{s->rbegin(), s->rend()}};
  }
  else if(get_if<null_t>(&in.v))
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

  while(!stack.empty())
  {
    auto& top = stack.back();
    if(top.idx >= top.list->size())
    {
      stack.pop_back();
      continue;
    }
    const value& e = (*top.list)[top.idx++];
    if(auto sub = get_if<list_type>(&e.v))
      stack.push_back({sub, 0});
    else
      dst.push_back(e);
  }
}

inline generator<value> b_flatten(const value& in)
{
  auto l = get_if<list_type>(&in.v);
  if(!l)
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
  if(kind_of(in) == K)
    co_yield in;
}

inline generator<value> b_scalars(const value& in)
{
  const auto k = kind_of(in);
  if(k != kind::array && k != kind::object)
    co_yield in;
}

inline generator<value> b_iterables(const value& in)
{
  const auto k = kind_of(in);
  if(k == kind::array || k == kind::object)
    co_yield in;
}

//! `empty`: the action that produces nothing.
inline generator<value> b_empty(const value&)
{
  co_return;
}

inline generator<value> b_tostring(const value& in)
{
  if(auto s = get_if<string_type>(&in.v))
  {
    co_yield *s;
  }
  else
  {
    std::string out;
    render_json(in, out);
    co_yield value{std::move(out)};
  }
}

inline generator<value> b_tonumber(const value& in)
{
  if(is_number(in))
  {
    co_yield in;
  }
  else if(auto s = get_if<string_type>(&in.v))
  {
    const auto d = parse_number(*s);
    if(!d)
      throw error{"Cannot parse '" + *s + "' as number"};
    co_yield number(*d);
  }
  else
  {
    throw error{std::string{type_name(in)} + " (" + brief(in)
                + ") cannot be parsed as a number"};
  }
}

template <typename F>
generator<value> numeric1(const value& in, F f, const char* name)
{
  if(!is_number(in))
    throw error{std::string{type_name(in)} + " (" + brief(in) + ") number required ("
                + name + ")"};
  co_yield number(f(as_number(in)));
}

// ------------------------------------------------- builtins with an argument

//! `map(f)`: [ .[] | f ]
inline generator<value> b_map(const value& in, const action_fun& f)
{
  auto l = get_if<list_type>(&in.v);
  if(!l)
    throw error{"Cannot iterate over " + std::string{type_name(in)}};
  list_type r;
  for(const auto& e : *l)
    for(auto& v : f(e))
      r.push_back(std::move(v.data));
  co_yield value{std::move(r)};
}

//! Key used to order by: jq takes *all* the outputs of f as the key, so
//! sort_by(.a, .b) orders by the pair.
inline value sort_key(const value& e, const action_fun& f)
{
  list_type k;
  for(auto& v : f(e))
    k.push_back(std::move(v.data));
  return value{std::move(k)};
}

inline generator<value> b_sort_by(const value& in, const action_fun& f)
{
  auto l = get_if<list_type>(&in.v);
  if(!l)
    throw error{std::string{type_name(in)} + " (" + brief(in) + ") cannot be sorted, as it is not an array"};

  config::vector<std::pair<value, value>> tmp;
  tmp.reserve(l->size());
  for(const auto& e : *l)
    tmp.emplace_back(sort_key(e, f), e);

  std::stable_sort(tmp.begin(), tmp.end(), [](const auto& a, const auto& b) {
    return compare(a.first, b.first) < 0;
  });

  list_type r;
  r.reserve(tmp.size());
  for(auto& [k, e] : tmp)
    r.push_back(std::move(e));
  co_yield value{std::move(r)};
}

inline generator<value> b_min_by(const value& in, const action_fun& f)
{
  auto l = get_if<list_type>(&in.v);
  if(!l)
    throw error{"Cannot iterate over " + std::string{type_name(in)}};
  if(l->empty())
  {
    co_yield value{null_t{}};
  }
  else
  {
    const value* best = &(*l)[0];
    value bestk = sort_key(*best, f);
    for(const auto& e : *l)
    {
      value k = sort_key(e, f);
      if(compare(k, bestk) < 0)
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
  if(!l)
    throw error{"Cannot iterate over " + std::string{type_name(in)}};
  if(l->empty())
  {
    co_yield value{null_t{}};
  }
  else
  {
    const value* best = &(*l)[0];
    value bestk = sort_key(*best, f);
    for(const auto& e : *l)
    {
      value k = sort_key(e, f);
      if(compare(k, bestk) >= 0)
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
  if(!l)
    throw error{std::string{type_name(in)} + " (" + brief(in) + ") cannot be grouped, as it is not an array"};

  config::vector<std::pair<value, value>> tmp;
  tmp.reserve(l->size());
  for(const auto& e : *l)
    tmp.emplace_back(sort_key(e, f), e);
  std::stable_sort(tmp.begin(), tmp.end(), [](const auto& a, const auto& b) {
    return compare(a.first, b.first) < 0;
  });

  list_type groups;
  for(std::size_t i = 0; i < tmp.size();)
  {
    list_type g;
    std::size_t j = i;
    while(j < tmp.size() && compare(tmp[j].first, tmp[i].first) == 0)
      g.push_back(std::move(tmp[j++].second));
    groups.push_back(value{std::move(g)});
    i = j;
  }
  co_yield value{std::move(groups)};
}

inline generator<value> b_has(const value& in, const action_fun& f)
{
  for(auto& k : f(in))
  {
    if(auto m = get_if<map_type>(&in.v))
    {
      auto s = get_if<string_type>(&k.data.v);
      if(!s)
        throw error{"Cannot check whether object has a key of type "
                    + std::string{type_name(k.data)}};
      co_yield value{m->find(*s) != m->end()};
    }
    else if(auto l = get_if<list_type>(&in.v))
    {
      if(!is_number(k.data))
        throw error{"Cannot check whether array has a key of type "
                    + std::string{type_name(k.data)}};
      const double i = as_number(k.data);
      co_yield value{i >= 0 && i < double(l->size())};
    }
    else
    {
      throw error{"Cannot check whether " + std::string{type_name(in)}
                  + " has a key"};
    }
  }
}

inline generator<value> b_any(const value& in)
{
  auto l = get_if<list_type>(&in.v);
  if(!l)
    throw error{"Cannot iterate over " + std::string{type_name(in)}};
  bool r = false;
  for(const auto& e : *l)
    if(truthy(e))
    {
      r = true;
      break;
    }
  co_yield value{r};
}

inline generator<value> b_all(const value& in)
{
  auto l = get_if<list_type>(&in.v);
  if(!l)
    throw error{"Cannot iterate over " + std::string{type_name(in)}};
  bool r = true;
  for(const auto& e : *l)
    if(!truthy(e))
    {
      r = false;
      break;
    }
  co_yield value{r};
}

inline generator<value> b_join(const value& in, const action_fun& f)
{
  auto l = get_if<list_type>(&in.v);
  if(!l)
    throw error{"Cannot iterate over " + std::string{type_name(in)}};

  for(auto& sepv : f(in))
  {
    auto sep = get_if<string_type>(&sepv.data.v);
    if(!sep)
      throw error{std::string{type_name(sepv.data)} + " cannot be used as a separator"};

    string_type out;
    bool first = true;
    for(const auto& e : *l)
    {
      if(!first)
        out += *sep;
      first = false;
      // null becomes the empty string; other non-strings are rendered.
      switch(kind_of(e))
      {
        case kind::null:
          break;
        case kind::string:
          out += *get_if<string_type>(&e.v);
          break;
        case kind::array:
        case kind::object:
          throw error{"Cannot join with " + std::string{type_name(e)}};
        default: {
          std::string s;
          render_json(e, s);
          out += s;
          break;
        }
      }
    }
    co_yield value{std::move(out)};
  }
}

inline generator<value> b_split(const value& in, const action_fun& f)
{
  if(!get_if<string_type>(&in.v))
    throw error{"split input must be a string"};
  for(auto& sep : f(in))
    co_yield divide(in, sep.data);
}

}

namespace jk::action
{
/**
 * @brief Resolve a zero-argument builtin by name.
 *
 * Throws for an unknown name so that the parser can reject the program
 * outright: a filter that silently does nothing is much harder to diagnose
 * than one that refuses to load.
 */
inline action_fun make_builtin0(const std::string& name)
{
  const auto wrap = [&](auto fn) {
    return action_fun{[fn](const value& in) { return fn(in); }, "builtin"};
  };
  const auto num = [&](double (*fn)(double), const char* n) {
    return action_fun{
        [fn, n](const value& in) { return numeric1(in, fn, n); }, "builtin"};
  };

  if(name == "length")     return wrap(b_length);
  if(name == "type")       return wrap(b_type);
  if(name == "not")        return wrap(b_not);
  if(name == "keys")       return wrap(b_keys);
  if(name == "keys_unsorted") return wrap(b_keys);
  if(name == "values")     return wrap(b_values);
  if(name == "to_entries") return wrap(b_to_entries);
  if(name == "add")        return wrap(b_add);
  if(name == "min")        return wrap(b_min);
  if(name == "max")        return wrap(b_max);
  if(name == "sort")       return wrap(b_sort);
  if(name == "unique")     return wrap(b_unique);
  if(name == "reverse")    return wrap(b_reverse);
  if(name == "flatten")    return wrap(b_flatten);
  if(name == "empty")      return wrap(b_empty);

  if(name == "nulls")      return wrap(b_of_kind<kind::null>);
  if(name == "booleans")   return wrap(b_of_kind<kind::boolean>);
  if(name == "numbers")    return wrap(b_of_kind<kind::number>);
  if(name == "strings")    return wrap(b_of_kind<kind::string>);
  if(name == "arrays")     return wrap(b_of_kind<kind::array>);
  if(name == "objects")    return wrap(b_of_kind<kind::object>);
  if(name == "scalars")    return wrap(b_scalars);
  if(name == "iterables")  return wrap(b_iterables);
  if(name == "tostring")   return wrap(b_tostring);
  if(name == "tonumber")   return wrap(b_tonumber);
  if(name == "any")        return wrap(b_any);
  if(name == "all")        return wrap(b_all);

  // first/last are .[0] and .[-1] in jq, including their null-for-empty.
  if(name == "first")
    return action_fun{[](const value& in) { return access_array(0, in); }, "first"};
  if(name == "last")
    return action_fun{[](const value& in) { return access_array(-1, in); }, "last"};

  if(name == "floor") return num([](double d) { return std::floor(d); }, "floor");
  if(name == "ceil")  return num([](double d) { return std::ceil(d); }, "ceil");
  if(name == "round") return num([](double d) { return std::round(d); }, "round");
  if(name == "fabs")  return num([](double d) { return std::abs(d); }, "fabs");
  if(name == "sqrt")  return num([](double d) { return std::sqrt(d); }, "sqrt");

  throw error{name + "/0 is not defined"};
}

//! Resolve a one-argument builtin. The argument is itself a compiled
//! expression, evaluated against whatever the builtin decides to apply it to.
inline action_fun make_builtin1(const std::string& name, action_fun arg)
{
  const auto wrap = [&](auto fn) {
    return action_fun{
        [fn, arg](const value& in) { return fn(in, arg); }, "builtin1"};
  };

  if(name == "map")      return wrap(b_map);
  if(name == "select")   return wrap(select);
  if(name == "sort_by")  return wrap(b_sort_by);
  if(name == "min_by")   return wrap(b_min_by);
  if(name == "max_by")   return wrap(b_max_by);
  if(name == "group_by") return wrap(b_group_by);
  if(name == "has")      return wrap(b_has);
  if(name == "join")     return wrap(b_join);
  if(name == "split")    return wrap(b_split);

  // map(f) and select(f) cover the common uses of the generic forms; the
  // remaining ones are rejected rather than silently ignored.
  throw error{name + "/1 is not defined"};
}
}
