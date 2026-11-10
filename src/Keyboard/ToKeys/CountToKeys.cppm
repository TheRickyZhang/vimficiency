export module vf.Keyboard.ToKeys.CountToKeys;

import std.compat;
export import vf.Keyboard.PhysicalKeys;
export import vf.Types.CountPrefixLimits;

export {


namespace CountToKeys {
// Returns prebuilt key prefix for a count in [0, CountPrefixLimits::MAX_PREFIX_COUNT].
const PhysicalKeys& keysForCount(int count);

// Notation prefix for a count (e.g. count 3 → "3"). count 0 → empty.
std::string_view textForCount(int count);
}  // namespace CountToKeys

}  // export
