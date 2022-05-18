#pragma once
#include <jk/config.hpp>

namespace jk
{
struct value;
using string_type = config::string;
using list_type = config::vector<value>;
using map_type = config::map<string_type, value>;
using variant = config::variant<
  int64_t, double, bool, string_type,
  list_type, map_type
>;

struct value
{
  using wrapped_type = variant;
  variant v;

  value() = default;
  value(const value&) = default;
  value(value&&) noexcept = default;
  value& operator=(const value&) = default;
  value& operator=(value&&) noexcept = default;

  value(const variant& v): v{v} { }
  value(variant&& v) noexcept : v{std::move(v)} { }

  value(list_type&& v) noexcept : v{std::move(v)} { }
  value(map_type&& v) noexcept : v{std::move(v)} { }
  value(int64_t v) noexcept : v{std::move(v)} { }
  value(int v) noexcept : v{int64_t(v)} { }
  value(float v) noexcept : v{(double) std::move(v)} { }
  value(double v) noexcept : v{std::move(v)} { }
  value(bool v) noexcept : v{std::move(v)} { }
  value(string_type&& v) noexcept : v{std::move(v)} { }

  value& operator=(list_type&& v) { this->v = std::move(v); return *this; }
  value& operator=(map_type&& v) { this->v = std::move(v); return *this; }
  value& operator=(int64_t v) { this->v = std::move(v); return *this; }
  value& operator=(int v) { this->v = std::move(v); return *this; }
  value& operator=(float v) { this->v = std::move(v); return *this; }
  value& operator=(double v) { this->v = std::move(v); return *this; }
  value& operator=(bool v) { this->v = std::move(v); return *this; }
  value& operator=(string_type&& v) { this->v = std::move(v); return *this; }

  auto& operator=(variant&& other) noexcept { v = std::move(other); return *this; }
  operator variant&() noexcept { return v; }
  operator const variant&() const noexcept { return v; }
  operator variant&&() && noexcept { return std::move(v); }

  bool operator==(const value& other) const noexcept = default;
};

}
