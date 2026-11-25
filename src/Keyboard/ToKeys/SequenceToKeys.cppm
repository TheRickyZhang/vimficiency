export module vf.Keyboard.ToKeys.SequenceToKeys;

import std.compat;
export import vf.Keyboard.ToKeys.CommandToKeys;
export import vf.Keyboard.PhysicalKeys;

export {

// Used for physical key presses (calculating effort) only!
// For semantic tokenization, see parseMovements() in MovementInterpreter.

class SequenceToKeys {
public:
  // `names` are the `<...>` names without modifiers. `<C-…>`, `<S-…>`,
  // `<M-…>` and `<D-…>` are priced as the modifier keys plus the key they
  // hold, and every other byte by CHAR_TO_KEYS. The map must outlive the
  // tokenizer.
  explicit SequenceToKeys(const CommandToKeys &names);

  // A name with no price warns and adds nothing, so it is never priced as the
  // letters that spell it.
  PhysicalKeys tokenize(std::string_view s) const;

private:
  bool appendName(std::string_view name, PhysicalKeys &out) const;

  const CommandToKeys *names_;
};

}  // export
