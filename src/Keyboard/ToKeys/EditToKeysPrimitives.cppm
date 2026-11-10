export module vf.Keyboard.ToKeys.EditToKeysPrimitives;

import std.compat;
export import vf.Keyboard.ToKeys.EditToKeys;

export {

// Generate all combinations from multiple character slots
// Example: makeCombinations({"d", "c"}, {"i", "a"}, {"w", "W"})
//   -> {"diw", "diW", "daw", "daW", "ciw", "ciW", "caw", "caW"}
// Each string in the input can be multi-char (e.g., "ge" for a single motion)
EditToKeys makeCombinations(std::initializer_list<std::vector<std::string>> slots);

}  // export
