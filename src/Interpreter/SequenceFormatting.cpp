module vf.Interpreter.SequenceFormatting;

import std.compat;
import vf.Interpreter.SequenceParser;

std::string formatSequenceForDisplay(std::string_view seq) {
  if (seq.empty()) {
    return "";
  }

  // Best-effort display: Lua's format_sequence passes captured user_seq
  // here, so unparseable input is expected. Fall back to the raw string
  // rather than erroring — the user still sees their keystrokes.
  auto parsed = parseSequenceStrings(seq);
  if (!parsed || parsed->empty()) {
    return std::string(seq);
  }

  std::string result;
  for (size_t i = 0; i < parsed->size(); i++) {
    if (i > 0) {
      result += ' ';
    }
    result += (*parsed)[i];
  }
  return result;
}
