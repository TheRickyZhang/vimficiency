export module vf.Keyboard.ToKeys.MovementToKeys;

import std.compat;
export import vf.Keyboard.ToKeys.CharToKeys;
export import vf.Keyboard.ToKeys.CommandToKeys;
export import vf.Keyboard.ToKeys.SequenceToKeys;

export {

using MotionSet = std::unordered_set<std::string, StringHash, std::equal_to<>>;

// All supported vim motions (for parsing/validation)
extern const MotionSet ALL_MOTIONS;

// Global tokenizer built from the <...> key notations
const SequenceToKeys& globalSequenceToKeys();

}  // export
