export module vf.LuaExports.Payload;

import std.compat;
import vf.LuaExports.Common;
import vf.Types.Lines;

export namespace VF::LuaExports::payload {

// Copies a line array (any range of strings, e.g. a repeated bytes field).
// Rejects an empty array and any line holding a newline or NUL byte.
template <typename Range>
Expected<Lines> linesFrom(const Range& range) {
  Lines lines;
  for (const auto& line : range) {
    const std::string_view text = line;
    const std::string index = std::to_string(lines.size());
    if (text.contains('\n')) {
      return unexpectedError("line array contains newline byte at line " + index);
    }
    if (text.contains('\0')) {
      return unexpectedError("line array contains NUL byte at line " + index);
    }
    lines.push_back(std::string(text));
  }
  if (lines.empty()) {
    return unexpectedError("line array must contain at least one line");
  }
  return lines;
}

}  // namespace VF::LuaExports::payload
