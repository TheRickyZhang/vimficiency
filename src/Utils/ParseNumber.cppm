export module vf.Utils.ParseNumber;

import std.compat;

export {

// Whole-string numeric parses: trailing garbage is a failure.
std::optional<int> parseInt(std::string_view s);
std::optional<double> parseDouble(std::string_view s);

}  // export
