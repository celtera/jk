#pragma once
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <limits>
#include <memory>
#include <new>
#include <span>

#include <memory_resource>
#include <type_traits>

namespace jk
{
namespace detail
{
// Constant-initialised on purpose: a dynamic initialiser would require a TLS
// init function, which MinGW emits outside any COMDAT group and therefore
// duplicates in every translation unit that includes this header. The null
// state means "unselected" and current_resource() supplies the default.
inline thread_local std::pmr::memory_resource* selected_resource = nullptr;
}

[[nodiscard]] inline std::pmr::memory_resource* current_resource() noexcept
{
  auto* const resource = detail::selected_resource;
  return resource ? resource : std::pmr::new_delete_resource();
}

// Selection is local to this thread and does not change PMR's global default.
class allocation_scope
{
public:
  explicit allocation_scope(std::pmr::memory_resource* resource) noexcept
      : previous_{current_resource()}
  {
    detail::selected_resource = resource;
  }

  ~allocation_scope() { detail::selected_resource = previous_; }
  allocation_scope(const allocation_scope&) = delete;
  allocation_scope& operator=(const allocation_scope&) = delete;

private:
  std::pmr::memory_resource* previous_;
};

template <typename T>
class allocator
{
public:
  using value_type = T;
  using propagate_on_container_move_assignment = std::true_type;
  using propagate_on_container_swap = std::true_type;
  using is_always_equal = std::false_type;

  allocator() noexcept
      : resource_{current_resource()}
  {
  }
  explicit allocator(std::pmr::memory_resource* resource) noexcept
      : resource_{resource ? resource : std::pmr::new_delete_resource()}
  {
  }

  template <typename U>
  allocator(const allocator<U>& other) noexcept
      : resource_{other.resource()}
  {
  }

  [[nodiscard]] T* allocate(std::size_t count)
  {
    if (count > std::numeric_limits<std::size_t>::max() / sizeof(T))
      throw std::bad_array_new_length{};
    return static_cast<T*>(resource_->allocate(count * sizeof(T), alignof(T)));
  }

  void deallocate(T* pointer, std::size_t count) noexcept
  {
    resource_->deallocate(pointer, count * sizeof(T), alignof(T));
  }

  [[nodiscard]] allocator
  select_on_container_copy_construction() const noexcept
  {
    return allocator{};
  }

  [[nodiscard]] std::pmr::memory_resource* resource() const noexcept
  {
    return resource_;
  }

  template <typename U>
  bool operator==(const allocator<U>& other) const noexcept
  {
    return *resource_ == *other.resource();
  }

private:
  std::pmr::memory_resource* resource_;
};

// The pool recycles individual allocations during evaluation. Its upstream
// arena owns no storage: the caller's buffer must outlive this context. The
// default upstream refuses exhaustion rather than silently using the heap.
class evaluation_context
{
public:
  explicit evaluation_context(
      std::span<std::byte> storage,
      std::pmr::memory_resource* upstream = std::pmr::null_memory_resource())
      : arena_{
            storage.data(),
            storage.size(),
            upstream ? upstream : std::pmr::null_memory_resource()}
      , pool_{&arena_}
  {
  }

  ~evaluation_context() { assert(depth_ == 0); }
  evaluation_context(const evaluation_context&) = delete;
  evaluation_context& operator=(const evaluation_context&) = delete;

  [[nodiscard]] std::pmr::memory_resource* resource() noexcept
  {
    return &pool_;
  }

  // All allocations from this context must have been destroyed before reset.
  void reset() noexcept
  {
    assert(depth_ == 0);
    if (depth_ != 0)
      return;
    pool_.release();
    arena_.release();
  }

private:
  friend class evaluation_scope;
  std::pmr::monotonic_buffer_resource arena_;
  std::pmr::unsynchronized_pool_resource pool_;
  std::size_t depth_{};
};

// Declare before the generators and values that use its context. Reentrant
// entry into the same context cannot reset an outer evaluation's live data.
class evaluation_scope
{
public:
  explicit evaluation_scope(evaluation_context& context) noexcept
      : context_{context}
      , selection_{context.resource()}
  {
    ++context_.depth_;
  }

  ~evaluation_scope()
  {
    if (--context_.depth_ == 0)
      context_.reset();
  }

  evaluation_scope(const evaluation_scope&) = delete;
  evaluation_scope& operator=(const evaluation_scope&) = delete;

private:
  evaluation_context& context_;
  allocation_scope selection_;
};

namespace detail
{
struct alignas(std::max_align_t) frame_header
{
  std::pmr::memory_resource* resource;
  std::size_t bytes;
  std::size_t alignment;
};

inline std::size_t frame_offset(std::size_t alignment) noexcept
{
  return (sizeof(frame_header) + alignment - 1) & ~(alignment - 1);
}
}

[[nodiscard]] inline void* allocate_frame(
    std::size_t bytes,
    std::size_t alignment = alignof(std::max_align_t))
{
  alignment = std::max(alignment, alignof(detail::frame_header));
  assert((alignment & (alignment - 1)) == 0);
  const auto max_size = std::numeric_limits<std::size_t>::max();
  if (alignment - 1 > max_size - sizeof(detail::frame_header))
    throw std::bad_alloc{};
  const auto offset = detail::frame_offset(alignment);
  if (bytes > max_size - offset)
    throw std::bad_alloc{};
  auto* resource = current_resource();
  auto* allocation
      = static_cast<std::byte*>(resource->allocate(offset + bytes, alignment));
  auto* frame = allocation + offset;
  ::new (static_cast<void*>(frame - sizeof(detail::frame_header)))
      detail::frame_header{resource, offset + bytes, alignment};
  return frame;
}

inline void deallocate_frame(void* pointer) noexcept
{
  if (!pointer)
    return;
  auto* frame = static_cast<std::byte*>(pointer);
  const auto header = *reinterpret_cast<const detail::frame_header*>(
      frame - sizeof(detail::frame_header));
  header.resource->deallocate(
      frame - detail::frame_offset(header.alignment),
      header.bytes,
      header.alignment);
}
}
