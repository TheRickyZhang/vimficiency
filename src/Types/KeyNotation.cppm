export module vf.Types.KeyNotation;

import std.compat;

export {

// Vim key-notation helpers. Convention in this codebase: a `std::string` /
// `std::string_view` holding a Vim sequence is in notation form — `<Space>`,
// `<CR>`, `<lt>`, etc. Lua normalises raw vim.on_key bytes via `keytrans`
// before they cross the FFI; internal helpers produce notation by construction.
// `Line`/`Lines` is the type for buffer text (the other string category that
// would otherwise be conflated with notation).

// Notation for a single literal char.
inline std::string displayChar(char c) {
  switch (c) {
    case ' ':  return "<Space>";
    case '\t': return "<Tab>";
    case '\n':
    case '\r': return "<CR>";
    case '<':  return "<lt>";
    default:   return std::string(1, c);
  }
}

// Every `<...>` name of a literal character (`:h key-notation`). `keytrans()`
// writes Space, Tab, both line breaks, `<`, `\` and `|` this way.
struct CharName {
  std::string_view name;
  char ch;
};
inline constexpr CharName CHAR_NAMES[] = {
    {"<Space>", ' '}, {"<Tab>", '\t'}, {"<CR>", '\n'}, {"<Enter>", '\n'}, {"<Return>", '\n'},
    {"<NL>", '\n'}, {"<lt>", '<'}, {"<LT>", '<'}, {"<Bslash>", '\\'}, {"<Bar>", '|'},
};

inline std::optional<char> parseDisplayChar(std::string_view s) {
  if (s.size() == 1) return s[0];
  for (const CharName& named : CHAR_NAMES) {
    if (named.name == s) return named.ch;
  }
  return std::nullopt;
}

// Splits a sequence into keys: `<...>` names and single UTF-8 characters. It
// knows no commands; a `<` that opens no name is a character.
inline std::vector<std::string_view> splitKeys(std::string_view keys) {
  std::vector<std::string_view> out;
  while (!keys.empty()) {
    size_t size = 1;
    if (keys.front() == '<') {
      const size_t close = keys.find_first_of("<>", 1);
      if (close != std::string_view::npos && keys[close] == '>') size = close + 1;
    } else {
      while (size < keys.size() && (keys[size] & 0xC0) == 0x80) size++;
    }
    out.push_back(keys.substr(0, size));
    keys.remove_prefix(size);
  }
  return out;
}

// For a key from `splitKeys`: a name, not a character.
inline bool isKeyName(std::string_view key) {
  return key.size() > 1 && key.front() == '<';
}

// For a key from `splitKeys`: it types a character, named or not.
inline bool isCharacterKey(std::string_view key) {
  return !isKeyName(key) || parseDisplayChar(key).has_value();
}

}  // export
