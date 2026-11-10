module;
#include <fast_float/fast_float.h>

module vf.Utils.ParseNumber;

import std.compat;

std::optional<int> parseInt(std::string_view s) {
  int v = 0;
  const auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), v);
  if (ec != std::errc{} || ptr != s.data() + s.size()) return std::nullopt;
  return v;
}

std::optional<double> parseDouble(std::string_view s) {
  double v = 0.0;
  const auto [ptr, ec] = fast_float::from_chars(s.data(), s.data() + s.size(), v);
  if (ec != std::errc{} || ptr != s.data() + s.size()) return std::nullopt;
  return v;
}
