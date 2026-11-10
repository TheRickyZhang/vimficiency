export module vf.Keyboard.ToKeys.MovementToKeys;

import std.compat;
export import vf.Keyboard.ToKeys.CharToKeys;
export import vf.Keyboard.ToKeys.CommandToKeys;
export import vf.Keyboard.ToKeys.SequenceToKeys;

export {

// std::less<> enables transparent comparison - allows lookup with string_view
// without allocating a temporary std::string
using MovementToKeys = CommandToKeys;

// =============================================================================
// Motion Maps
// =============================================================================

// All physical key/action mappings (for tokenizing raw input)
extern const MovementToKeys ACTION_MOTIONS_TO_KEYS;

// All supported vim motions (for parsing/validation)
extern const MovementToKeys ALL_MOTIONS;

// Motions that can be directly explored in optimizer search loop.
// Excludes motions needing special handling (f/F/t/T require target char, ;/, require prior context)
extern const MovementToKeys EXPLORABLE_MOTIONS;


// =============================================================================
// Utilities
// =============================================================================

MovementToKeys getSlicedMovementToKeys(std::vector<std::string> motions);

// Global tokenizer built from the action and motion maps
const SequenceToKeys& globalSequenceToKeys();

}  // export
