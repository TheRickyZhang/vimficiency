export module vf.Keyboard.ToKeys.CharToKeys;

import std.compat;
export import vf.Keyboard.PhysicalKeys;

export {

// Single-character to PhysicalKeys mapping
using CharToKeys = std::unordered_map<char, PhysicalKeys>;

// All printable characters mapped to their key sequences
extern const CharToKeys CHAR_TO_KEYS;

}  // export
