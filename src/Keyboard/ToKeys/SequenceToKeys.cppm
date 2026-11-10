export module vf.Keyboard.ToKeys.SequenceToKeys;

import std.compat;
export import vf.Keyboard.ToKeys.CommandToKeys;
export import vf.Keyboard.PhysicalKeys;

export {

// Used for physical key presses (calculating effort) only!
// For semantic tokenization, see parseMovements() in MovementInterpreter.

class SequenceToKeys {
public:
  // `notations` are the multi-char tokens matched longest-first; every other
  // byte falls back to CHAR_TO_KEYS. The map must outlive the tokenizer.
  explicit SequenceToKeys(const CommandToKeys &notations);

  PhysicalKeys tokenize(std::string_view s) const;

private:
  struct TokenDef {
    std::string      token;
    const PhysicalKeys *keys; // non-owning, points into mappings
  };

  std::vector<TokenDef> tokens_; // sorted by descending token length
};

}  // export
