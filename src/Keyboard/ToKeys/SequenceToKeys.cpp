module vf.Keyboard.ToKeys.SequenceToKeys;

import std.compat;
import vf.Keyboard.ToKeys.CharToKeys;
import vf.Utils.Debug;

namespace {
// Render `[start, end)` of `s` as `byte/hex` pairs so non-printable bytes
// (e.g. K_SPECIAL 0x80) survive the trip through `vim.notify`.
std::string hexWindow(std::string_view s, size_t start, size_t end) {
  char buf[8];
  std::string out;
  out.reserve((end - start) * 5);
  for (size_t k = start; k < end; ++k) {
    const auto byte = static_cast<unsigned>(static_cast<unsigned char>(s[k]));
    std::snprintf(buf, sizeof(buf), "%02x ", byte);
    out += buf;
  }
  if (!out.empty()) out.pop_back();
  return out;
}
}  // namespace

using namespace std;

SequenceToKeys::SequenceToKeys(const CommandToKeys &names) : names_(&names) {}

bool SequenceToKeys::appendName(string_view name, PhysicalKeys &out) const {
  if (const auto it = names_->find(name); it != names_->end()) {
    out.append(it->second);
    return true;
  }

  string_view held = name.substr(1, name.size() - 2);
  vector<Key> keys;
  bool ctrl = false;
  while (held.size() > 2 && held[1] == '-') {
    switch (held[0]) {
      case 'C': keys.push_back(Key::Key_Ctrl); ctrl = true; break;
      case 'S': keys.push_back(Key::Key_Shift); break;
      case 'M':
      case 'A': keys.push_back(Key::Key_Alt); break;
      default: return false;
    }
    held.remove_prefix(2);
  }
  if (keys.empty()) return false;

  const PhysicalKeys *base = nullptr;
  if (held.size() == 1) {
    // `<C-A>` is Ctrl with the A key; a held Shift is written `<C-S-A>`.
    const char c = ctrl ? tolower(held[0]) : held[0];
    if (const auto it = CHAR_TO_KEYS.find(c); it != CHAR_TO_KEYS.end()) base = &it->second;
  } else if (const auto it = names_->find(format("<{}>", held)); it != names_->end()) {
    base = &it->second;
  }
  if (!base) return false;

  for (const Key key : *base) {
    if (!ranges::contains(keys, key)) keys.push_back(key);
  }
  out.append(PhysicalKeys(std::move(keys)));
  return true;
}

PhysicalKeys SequenceToKeys::tokenize(string_view s) const {
  PhysicalKeys out;
  size_t i=0;

  while(i < s.size()){
    if (s[i] == '<') {
      const size_t close = s.find_first_of("<>", i + 1);
      if (close != string_view::npos && s[close] == '>' && close > i + 1) {
        appendName(s.substr(i, close + 1 - i), out);
        i = close + 1;
        continue;
      }
    }
    {
      char ch = s[i];
      auto it = CHAR_TO_KEYS.find(ch);
      if (it != CHAR_TO_KEYS.end()) {
        out.append(it->second);
        i++;
      } else {
        // Soft-fail: typed text can contain bytes with no token or
        // CHAR_TO_KEYS mapping (e.g. UTF-8 continuation bytes, or
        // K_SPECIAL-encoded keys that bypassed Lua-side keytrans).
        // Emit a hex window so the surrounding bytes are inspectable
        // even when they're non-printable, and skip so cost computation
        // continues — under-counting one byte beats crashing.
        const size_t winStart = i >= 4 ? i - 4 : 0;
        const size_t winEnd = std::min(i + 4, s.size());
        char hex[8];
        std::snprintf(hex, sizeof(hex), "0x%02x",
                      static_cast<unsigned>(static_cast<unsigned char>(s[i])));
        warning("SequenceToKeys::tokenize: unknown byte", hex,
                "at index", i, "of", s.size(),
                "; window[", winStart, ":", winEnd, "]=",
                hexWindow(s, winStart, winEnd));
        i++;
      }
    }
  }
  return out;
}
