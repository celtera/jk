#pragma once
#include <jk/value.hpp>

#include <functional>

#if __has_include(<avnd/common/coroutines.hpp>)
#include <avnd/common/coroutines.hpp>
#else
#if __has_include(<coroutine>)
#include <coroutine>
#elif __has_include(<experimental/coroutine>)
#include <experimental/coroutine>
namespace std
{
using suspend_always = std::experimental::suspend_always;
template <typename T = void>
using coroutine_handle = std::experimental::coroutine_handle<T>;
}
#else
#error No coroutine support
#endif
#endif

namespace jk
{
// FIXME replace with std::generator when it's out
template <typename Out>
class generator
{
public:
  struct promise_type
  {
    Out data;
    generator get_return_object()
    {
      return generator{handle::from_promise(*this)};
    }

    static std::suspend_always initial_suspend() noexcept { return {}; }
    static std::suspend_always final_suspend() noexcept { return {}; }

    template <typename T>
    std::suspend_always yield_value(T&& value) noexcept
    {
      data = std::move(value);
      return std::suspend_always{};
    }

    void return_void() noexcept { }
    void await_transform() = delete;

    [[noreturn]] static void unhandled_exception() { std::abort(); }
  };

  using handle = std::coroutine_handle<promise_type>;

  generator() noexcept = default;
  generator(const generator&) = delete;
  generator& operator=(const generator&) = delete;

  generator(generator&& other) noexcept
      : m_coroutine{other.m_coroutine}
  {
    other.m_coroutine = {};
  }

  generator& operator=(generator&& other) noexcept
  {
    if (this != &other)
    {
      if (m_coroutine)
        m_coroutine.destroy();

      m_coroutine = other.m_coroutine;
      other.m_coroutine = {};
    }
    return *this;
  }

  ~generator()
  {
    if (m_coroutine)
      m_coroutine.destroy();
  }

  class iterator
  {
  public:
    explicit iterator(const handle& coroutine) noexcept
        : m_coroutine{coroutine}
    {
    }

    void operator++() noexcept { m_coroutine.resume(); }
    auto& operator*() const noexcept { return m_coroutine.promise(); }
    bool operator==(std::default_sentinel_t) const noexcept
    {
      return !m_coroutine || m_coroutine.done();
    }

  private:
    handle m_coroutine;
  };

  explicit generator(handle coroutine)
      : m_coroutine{std::move(coroutine)}
  {
  }

  [[nodiscard]] iterator begin() noexcept
  {
    if (m_coroutine)
      m_coroutine.resume();

    return iterator{m_coroutine};
  }

  [[nodiscard]] std::default_sentinel_t end() const noexcept { return {}; }

private:
  handle m_coroutine;
};

struct action_fun : std::function<generator<value>(const value& in)>
{
  std::string name;
};

}
