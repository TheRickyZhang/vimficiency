export module vf.Types.Sequence;

import std.compat;

export {

// Represents a sequence of Vim commands/keys in notation form (e.g.
// `<Space>`, `<CR>`, `<lt>`). See `Types/KeyNotation.h` for the convention.
class Sequence {
  std::string keys;
public:

  Sequence() = default;
  inline Sequence(const std::string& k) : keys(k) {}
  inline Sequence(std::string&& k) : keys(std::move(k)) {}

  inline bool empty() const { return keys.empty(); }
  inline size_t size() const { return keys.size(); }

  inline void append(char c, int count = 1) { keys.append(count, c); }
  inline void append(std::string_view s, int count = 1) {
    for (int i = 0; i < count; i++) keys += s;
  }

  inline bool operator==(const Sequence& other) const { return keys == other.keys; }
  inline bool operator!=(const Sequence& other) const { return !(*this == other); }
  inline bool operator==(const std::string& s) const { return keys == s; }
  inline bool operator!=(const std::string& s) const { return keys != s; }
  inline bool operator==(const char* s) const { return keys == s; }
  inline bool operator!=(const char* s) const { return keys != s; }
  inline bool operator<(const Sequence& other) const { return keys < other.keys; }

  // Non-owning view for read-side consumers (streams, FFI, comparison).
  inline std::string_view view() const { return keys; }

  // Use when const string& is needed (string concatenation with +,
  // APIs taking const string&, constructing owned copies).
  inline const std::string& str() const { return keys; }

  // Stream output with mode-separated spacing
  // Parses the sequence to identify mode transitions (insert entries via
  // i/I/a/A/o/O/s/S/C/R/c{motion}, insert exits via <Esc>) and inserts
  // spaces between segments.
  friend std::ostream& operator<<(std::ostream& os, const Sequence& seq);
};

}  // export
