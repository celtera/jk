#pragma once
#include <jk/memory.hpp>
#include <jk/value.hpp>

#include <exception>
#include <optional>
#include <utility>

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
//! A result is valid until its producer advances or is destroyed. Forwarding
//! preserves both the borrow and, when present, the original owned storage.
template <typename T>
class yielded
{
public:
  yielded() = default;
  explicit yielded(const T& value, T* owned = nullptr) noexcept
      : m_value{&value}
      , m_owned{owned}
  {
  }

  const T& get() const noexcept { return *m_value; }

  //! Consume once when retaining a result beyond the next producer resume.
  T take()
  {
    if (m_owned)
      return std::move(*m_owned);
    return *m_value;
  }

  //! Detach from all evaluation storage before crossing an output boundary.
  T persist() const
  {
    allocation_scope persistent{nullptr};
    return *m_value;
  }

private:
  const T* m_value{};
  T* m_owned{};
};

// FIXME replace with std::generator when it's out
template <typename Out>
class generator
{
public:
  struct promise_type
  {
    // A user-declared constructor prevents aggregate initialization from
    // copying a coroutine's input into its promise before the first resume.
    promise_type() = default;

    static void* operator new(std::size_t size)
    {
      return allocate_frame(size, alignof(promise_type));
    }
    static void operator delete(void* pointer, std::size_t) noexcept
    {
      deallocate_frame(pointer);
    }

    std::optional<Out> owned;
    yielded<Out> current;
    generator get_return_object()
    {
      return generator{handle::from_promise(*this)};
    }

    static std::suspend_always initial_suspend() noexcept { return {}; }
    static std::suspend_always final_suspend() noexcept { return {}; }

    std::suspend_always yield_value(const Out& value) noexcept
    {
      owned.reset();
      current = yielded<Out>{value};
      return {};
    }

    std::suspend_always yield_value(Out&& value)
    {
      owned.emplace(std::move(value));
      current = yielded<Out>{*owned, &*owned};
      return {};
    }

    std::suspend_always yield_value(const yielded<Out>& value) noexcept
    {
      owned.reset();
      current = value;
      return {};
    }

    void return_void() noexcept { }
    void await_transform() = delete;

    //! Capture rather than abort. A jq type error has to unwind out of the
    //! program - and a coroutine that aborts on any exception would take the
    //! host process down with it on something as ordinary as bad_alloc.
    std::exception_ptr exception{};
    void unhandled_exception() noexcept
    {
      exception = std::current_exception();
    }
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

    void operator++()
    {
      m_coroutine.resume();
      rethrow_if_failed();
    }

    //! The coroutine stops at its final suspend after throwing, so the
    //! exception has to be surfaced when the consumer next looks at it.
    void rethrow_if_failed() const
    {
      if (m_coroutine && m_coroutine.done())
        if (auto& e = m_coroutine.promise().exception)
          std::rethrow_exception(std::exchange(e, {}));
    }
    auto& operator*() const noexcept { return m_coroutine.promise().current; }
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

  [[nodiscard]] iterator begin()
  {
    if (m_coroutine)
      m_coroutine.resume();

    iterator it{m_coroutine};
    it.rethrow_if_failed();
    return it;
  }

  [[nodiscard]] std::default_sentinel_t end() const noexcept { return {}; }

private:
  handle m_coroutine;
};

}
