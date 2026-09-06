#pragma once

#include <jk/generator.hpp>
#include <smallfun.hpp>

#include <functional>
#include <memory>
#include <utility>

#include <type_traits>

namespace jk
{
//! Moving a program handle never relocates closures referenced by suspended
//! coroutines. The program must outlive every evaluation of its actions.
class action_fun
{
  using function_type = smallfun::function<
      generator<value>(const value&),
      128,
      smallfun::DefaultAlign,
      smallfun::Methods::Move>;

  struct node
  {
    function_type function;

    template <typename F>
    explicit node(F&& f)
        : function{std::decay_t<F>(std::forward<F>(f))}
    {
    }
  };

  std::unique_ptr<node> m_node;

public:
  action_fun() noexcept = default;
  action_fun(const action_fun&) = delete;
  action_fun& operator=(const action_fun&) = delete;
  action_fun(action_fun&&) noexcept = default;
  action_fun& operator=(action_fun&&) noexcept = default;

  template <typename F>
    requires(
        !std::is_same_v<std::remove_cvref_t<F>, action_fun>
        && std::
            is_invocable_r_v<generator<value>, std::decay_t<F>&, const value&>)
  explicit action_fun(F&& f)
      : m_node{std::make_unique<node>(std::forward<F>(f))}
  {
  }

  explicit operator bool() const noexcept { return bool(m_node); }

  generator<value> operator()(const value& in) const
  {
    if (!m_node)
      throw std::bad_function_call{};
    return m_node->function(in);
  }
};
}
