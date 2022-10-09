#pragma once
#include <jk/config.hpp>

namespace jk
{
struct value;
using string_type = config::string;
using list_type = config::vector<value>;
using map_type = config::map<string_type, value>;
using variant
    = config::variant<int64_t, double, bool, string_type, list_type, map_type>;

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
  value(int64_t v) noexcept : v{v} { }
  value(int v) noexcept : v{int64_t(v)} { }
  value(float v) noexcept : v{static_cast<double>(v)} { }
  value(double v) noexcept : v{v} { }
  value(bool v) noexcept : v{v} { }
  value(const string_type& v) noexcept : v{v} { }
  value(string_type&& v) noexcept : v{std::move(v)} { }
  value(const string_type::value_type* v) noexcept : v{string_type{v}} { }
  value(void* v) = delete;
  value(std::nullptr_t v) = delete;

  value& operator=(list_type&& in) { this->v = std::move(in); return *this; }
  value& operator=(map_type&& in) { this->v = std::move(in); return *this; }
  value& operator=(const list_type& in) { this->v = in; return *this; }
  value& operator=(const map_type& in) { this->v = in; return *this; }
  value& operator=(int64_t in) { this->v = in; return *this; }
  value& operator=(int in) { this->v = in; return *this; }
  value& operator=(float in) { this->v = in; return *this; }
  value& operator=(double in) { this->v = in; return *this; }
  value& operator=(bool in) { this->v = in; return *this; }
  value& operator=(const string_type& in) { this->v = in; return *this; }
  value& operator=(string_type&& in) { this->v = std::move(in); return *this; }
  value& operator=(const char* in) { this->v = string_type{in}; return *this; }

  auto& operator=(variant&& other) noexcept { v = std::move(other); return *this; }
  operator variant&() noexcept { return v; }
  operator const variant&() const noexcept { return v; }
  operator variant&&() && noexcept { return std::move(v); }

  bool operator==(const value& other) const noexcept = default;
};

// clang-format on
}
