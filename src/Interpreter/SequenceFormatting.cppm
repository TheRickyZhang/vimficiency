export module vf.Interpreter.SequenceFormatting;

import std.compat;

export {

// Format a Vim sequence string for human-readable display by tokenizing into
// logical command units and joining with spaces.
std::string formatSequenceForDisplay(std::string_view seq);

}  // export
