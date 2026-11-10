export module vf.Keyboard.ToKeys.CommandToKeys;

import std.compat;
export import vf.Keyboard.ToKeys.CharToKeys;
export import vf.Keyboard.PhysicalKeys;

export {

// Allow unordered_map<string, ...> to use alternative equivalent types, like string_view
struct StringHash {
  // All of std::string, const char*, std::string_view are convertable to string_view
  using is_transparent = void;
  inline size_t operator()(const std::string_view sv) const {
    return std::hash<std::string_view>{}(sv);
  }
};

using CommandToKeys = std::unordered_map<std::string, PhysicalKeys, StringHash, std::equal_to<>>;

// Merge multiple CommandToKeys maps into one. reference_wrapper avoids copying
// while still giving the initializer list concrete objects.
CommandToKeys combineAll(std::initializer_list<std::reference_wrapper<const CommandToKeys>> maps);

}  // export
