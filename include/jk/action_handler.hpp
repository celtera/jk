#pragma once
#include <boost/fusion/container.hpp>
#include <boost/fusion/sequence/intrinsic/at_c.hpp>

#include <jk/actions.hpp>
#include <jk/builtins.hpp>

#include <string>
#include <utility>
#include <vector>

/**
 * \file action_handler.hpp
 *
 * What the grammar's semantic actions drive.
 *
 * This is a stack machine: a rule that parses a complete sub-expression
 * pushes one action, and a rule that combines two of them pops its operands
 * and pushes the result. Nesting then costs nothing - a parenthesised
 * expression, an array constructor and a function argument all push and pop
 * in the order they are parsed.
 *
 * Everything is built at parse time. A message being filtered only walks the
 * resulting tree of std::function, so none of the composition here costs
 * anything per message.
 */
namespace jk::actions
{
struct handlers
{
  //! action_fun derives from std::function and adds a name, so building one
  //! needs the base to get its own braces. This says it once.
  template <typename F>
  static action_fun mk(F&& f, std::string n)
  {
    return action_fun{{std::forward<F>(f)}, std::move(n)};
  }

  //! Operand stack.
  std::vector<action_fun> stack;

  //! Object literals under construction, one frame per nesting level.
  std::vector<std::vector<std::pair<action_fun, action_fun>>> obj_frames;

  //! The finished program. Named for the two call sites that already read it.
  std::vector<action_fun> current_seq;

  // ------------------------------------------------------------- utilities

  void push(action_fun a) { stack.push_back(std::move(a)); }

  action_fun pop()
  {
    if(stack.empty())
      return {[](const value& in) { return action::copy_all(in); }, "identity"};
    auto a = std::move(stack.back());
    stack.pop_back();
    return a;
  }

  //! Pop two operands and push the result of combining them. The left operand
  //! was pushed first, so it is the deeper of the two.
  template <typename F>
  void combine(const char* name, F&& make)
  {
    auto rhs = pop();
    auto lhs = pop();
    push(mk(make(std::move(lhs), std::move(rhs)), name));
  }

  // -------------------------------------------------------------- primaries

  void identity(auto)
  {
    push(mk([](const value& in) { return action::copy_all(in); }, "identity"));
  }

  void recurse(auto)
  {
    push(mk([](const value& in) { return action::recurse(in); }, "recurse"));
  }

  void literal(value v)
  {
    push(mk([v = std::move(v)](const value&) { return action::constant(v); },
            "literal"));
  }

  void lit_null(auto) { literal(value{null_t{}}); }
  void lit_true(auto) { literal(value{true}); }
  void lit_false(auto) { literal(value{false}); }
  void lit_int(int64_t v) { literal(value{v}); }
  void lit_double(double v) { literal(number(v)); }
  void lit_string(const std::string& v) { literal(value{v}); }

  //! An empty `[]` collects nothing, so it is not the same as `[ . ]`.
  void empty_array(auto)
  {
    push(mk([](const value&) { return action::constant(value{list_type{}}); },
            "empty_array"));
  }

  // --------------------------------------------------------------- suffixes
  //
  // Each of these consumes the expression parsed so far and replaces it with
  // that expression piped into the access.

  template <typename F>
  void suffix(const char* name, F&& make)
  {
    auto base = pop();
    action_fun step = mk(make(), name);
    push(mk(
        [base = std::move(base), step = std::move(step)](const value& in) {
          return action::compose(in, base, step);
        },
        name));
  }

  void access_member(const std::string& key)
  {
    suffix("access_member", [&] {
      return [key](const value& in) { return action::access_member(key, in); };
    });
  }

  void access_array(int idx)
  {
    suffix("access_array", [&] {
      return [idx](const value& in) { return action::access_array(idx, in); };
    });
  }

  void access_array_pipe(const std::vector<int>& idx)
  {
    suffix("access_array_pipe", [&] {
      return [idx](const value& in) {
        return action::access_array_indices(idx, in);
      };
    });
  }

  void access_array_range(auto&& idx)
  {
    const std::pair<int, int> r{
        boost::fusion::at_c<0>(idx), boost::fusion::at_c<1>(idx)};
    suffix("access_array_range", [&] {
      return [r](const value& in) {
        return action::access_array_range(r.first, r.second, true, true, in);
      };
    });
  }

  //! `.[a:]` and `.[:b]`, where the missing end means "as far as it goes".
  void access_array_range_from(int a)
  {
    suffix("access_array_range_from", [&] {
      return [a](const value& in) {
        return action::access_array_range(a, 0, true, false, in);
      };
    });
  }

  void access_array_range_to(int b)
  {
    suffix("access_array_range_to", [&] {
      return [b](const value& in) {
        return action::access_array_range(0, b, false, true, in);
      };
    });
  }

  void iterate(auto)
  {
    suffix("iterate", [&] {
      return [](const value& in) { return action::iterate_array(in); };
    });
  }

  //! `.[expr]`: index by a computed key, which is how a dynamic field lookup
  //! is spelled.
  void index_by_expr(auto)
  {
    auto key = pop();
    auto base = pop();
    push(mk(
        [base = std::move(base), key = std::move(key)](const value& in) {
          return action::index_by(in, base, key);
        },
        "index_by_expr"));
  }

  void optional(auto)
  {
    auto base = pop();
    push(mk(
        [base = std::move(base)](const value& in) {
          return action::optional(in, base);
        },
        "optional"));
  }

  // -------------------------------------------------------------- operators

  void pipe(auto)
  {
    combine("pipe", [](action_fun l, action_fun r) {
      return [l = std::move(l), r = std::move(r)](const value& in) {
        return action::compose(in, l, r);
      };
    });
  }

  void comma(auto)
  {
    combine("comma", [](action_fun l, action_fun r) {
      return [l = std::move(l), r = std::move(r)](const value& in) {
        return action::concat(in, l, r);
      };
    });
  }

  void alternative(auto)
  {
    combine("alternative", [](action_fun l, action_fun r) {
      return [l = std::move(l), r = std::move(r)](const value& in) {
        return action::alternative(in, l, r);
      };
    });
  }

  void op_or(auto)
  {
    combine("or", [](action_fun l, action_fun r) {
      return [l = std::move(l), r = std::move(r)](const value& in) {
        return action::logical_or(in, l, r);
      };
    });
  }

  void op_and(auto)
  {
    combine("and", [](action_fun l, action_fun r) {
      return [l = std::move(l), r = std::move(r)](const value& in) {
        return action::logical_and(in, l, r);
      };
    });
  }

  template <typename Pred>
  void compare_with(const char* name, Pred pred)
  {
    combine(name, [pred](action_fun l, action_fun r) {
      return [l = std::move(l), r = std::move(r), pred](const value& in) {
        return action::comparison(in, l, r, pred);
      };
    });
  }

  void op_eq(auto) { compare_with("eq", [](int c) { return c == 0; }); }
  void op_ne(auto) { compare_with("ne", [](int c) { return c != 0; }); }
  void op_lt(auto) { compare_with("lt", [](int c) { return c < 0; }); }
  void op_le(auto) { compare_with("le", [](int c) { return c <= 0; }); }
  void op_gt(auto) { compare_with("gt", [](int c) { return c > 0; }); }
  void op_ge(auto) { compare_with("ge", [](int c) { return c >= 0; }); }

  template <typename Op>
  void arith(const char* name, Op op)
  {
    combine(name, [op](action_fun l, action_fun r) {
      return [l = std::move(l), r = std::move(r), op](const value& in) {
        return action::binary(in, l, r, op);
      };
    });
  }

  void op_add(auto) { arith("add", [](const value& a, const value& b) { return jk::add(a, b); }); }
  void op_sub(auto) { arith("sub", [](const value& a, const value& b) { return jk::subtract(a, b); }); }
  void op_mul(auto) { arith("mul", [](const value& a, const value& b) { return jk::multiply(a, b); }); }
  void op_div(auto) { arith("div", [](const value& a, const value& b) { return jk::divide(a, b); }); }
  void op_mod(auto) { arith("mod", [](const value& a, const value& b) { return jk::modulo(a, b); }); }

  void op_neg(auto)
  {
    auto a = pop();
    push(mk(
        [a = std::move(a)](const value& in) { return action::negate(in, a); },
        "neg"));
  }

  // ------------------------------------------------------------ constructors

  void create_array(auto)
  {
    auto inner = pop();
    push(mk(
        [inner = std::move(inner)](const value& in) {
          return action::as_array(in, inner);
        },
        "create_array"));
  }

  void begin_object(auto) { obj_frames.emplace_back(); }

  //! `{ foo }`, which jq expands to `{ foo: .foo }`.
  void object_shorthand(const std::string& key)
  {
    action_fun k{[key](const value&) { return action::constant(value{key}); },
                 "key"};
    action_fun v{[key](const value& in) { return action::access_member(key, in); },
                 "shorthand_value"};
    obj_frames.back().emplace_back(std::move(k), std::move(v));
  }

  //! `{ key: expr }` with a literal key.
  void object_member(const std::string& key)
  {
    auto v = pop();
    action_fun k{[key](const value&) { return action::constant(value{key}); },
                 "key"};
    obj_frames.back().emplace_back(std::move(k), std::move(v));
  }

  //! `{ (expr): expr }`: the key is computed too.
  void object_member_expr(auto)
  {
    auto v = pop();
    auto k = pop();
    obj_frames.back().emplace_back(std::move(k), std::move(v));
  }

  void create_object(auto)
  {
    auto members = std::move(obj_frames.back());
    obj_frames.pop_back();
    push(mk(
        [members = std::move(members)](const value& in) {
          return action::as_object(in, members);
        },
        "create_object"));
  }

  // -------------------------------------------------------------- functions

  void builtin0(const std::string& name)
  {
    push(action::make_builtin0(name));
  }

  void builtin1(const std::string& name)
  {
    auto arg = pop();
    push(action::make_builtin1(name, std::move(arg)));
  }

  // ----------------------------------------------------------------- finish

  void finish(auto)
  {
    current_seq.clear();
    current_seq.push_back(pop());
  }

  void clear()
  {
    stack.clear();
    obj_frames.clear();
    current_seq.clear();
  }
};
}
