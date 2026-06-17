#pragma once
#include <jk/config.hpp>

#include <cstddef>
#include <utility>

namespace jk
{
//! jq's null. First alternative on purpose: a default-constructed value is
//! then null, which is what jq produces for a missing key, an out-of-range
//! index, or an empty input - not the integer zero it used to default to.
struct null_t
{
  bool operator==(const null_t&) const noexcept = default;
  auto operator<=>(const null_t&) const noexcept = default;
};

struct value;
using string_type = config::string;
using list_type = config::vector<value>;
using map_type = config::map<string_type, value>;
using variant
    = config::variant<null_t, int64_t, double, bool, string_type, list_type, map_type>;

// clang-format off
struct value
{
  using wrapped_type = variant;
  variant v;

  value() = default;
  value(const value&) = default;
  value(value&&) noexcept = default;
  value& operator=(const value&) = default;
  value& operator=(value&&) noexcept = default;
  ~value() = default;

  value(const variant& v): v{v} { }
  value(variant&& v) noexcept : v{std::move(v)} { }

  value(const list_type& v) noexcept : v{v} { }
  value(const map_type& v) noexcept : v{v} { }
  value(list_type&& v) noexcept : v{std::move(v)} { }
  value(map_type&& v) noexcept : v{std::move(v)} { }
  value(null_t v) noexcept : v{v} { }
  value(int64_t v) noexcept : v{v} { }
  value(int v) noexcept : v{int64_t(v)} { }
  value(float v) noexcept : v{static_cast<double>(v)} { }
  value(double v) noexcept : v{v} { }
  value(bool v) noexcept : v{v} { }
  value(const string_type& v) noexcept : v{v} { }
  value(string_type&& v) noexcept : v{std::move(v)} { }
  value(const string_type::value_type* v) noexcept : v{string_type{v}} { }
  value(void* v) = delete;
  value(const void* v) = delete;
  value(std::nullptr_t v) = delete;

  value& operator=(list_type&& in) { this->v = std::move(in); return *this; }
  value& operator=(map_type&& in) { this->v = std::move(in); return *this; }
  value& operator=(const list_type& in) { this->v = in; return *this; }
  value& operator=(const map_type& in) { this->v = in; return *this; }
  value& operator=(int64_t in) { this->v = in; return *this; }
  value& operator=(int in) { this->v = (int64_t)in; return *this; }
  value& operator=(float in) { this->v = (double)in; return *this; }
  value& operator=(double in) { this->v = in; return *this; }
  value& operator=(bool in) { this->v = in; return *this; }
  value& operator=(const string_type& in) { this->v = in; return *this; }
  value& operator=(string_type&& in) { this->v = std::move(in); return *this; }
  value& operator=(const char* in) { this->v = string_type{in}; return *this; }
  value& operator=(void* in) = delete;
  value& operator=(const void* in) = delete;

  auto& operator=(variant&& other) noexcept { v = std::move(other); return *this; }
  operator variant&() noexcept { return v; }
  operator const variant&() const noexcept { return v; }
  operator variant&&() && noexcept { return std::move(v); }

  bool operator==(const value& other) const noexcept = default;

  // Model avnd::variant_ish: the Avendish back-end bindings (pd / max / wasm)
  // treat a value output as a variant (concept check + unqualified visit). Expose
  // the variant interface so jk::value is usable directly, without libossia.
  std::size_t index() const noexcept { return v.index(); }
  bool valueless_by_exception() const noexcept { return v.valueless_by_exception(); }
};

// ADL hook for the bindings' unqualified visit(f, value): forward to the
// configured variant's visit on the wrapped member.
template <typename F>
inline decltype(auto) visit(F&& f, value& self)
{
  return config::variant_ns::visit(std::forward<F>(f), self.v);
}
template <typename F>
inline decltype(auto) visit(F&& f, const value& self)
{
  return config::variant_ns::visit(std::forward<F>(f), self.v);
}
template <typename F>
inline decltype(auto) visit(F&& f, value&& self)
{
  return config::variant_ns::visit(std::forward<F>(f), std::move(self.v));
}

// clang-format on
}
