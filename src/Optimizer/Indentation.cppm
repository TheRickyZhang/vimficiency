export module vf.Optimizer.Indentation;

import std.compat;
export import vf.Keyboard.KeyedSequence;
export import vf.VimCore.VimOptions;

export {

inline int bsCountForIndent(int from, int to, int sw) {
  if (sw <= 0 || to > from || (to != from && to % sw != 0)) return -1;
  return from == to ? 0 : (from - 1) / sw + 1 - to / sw;
}

// Quoting a literal tab prevents Insert-mode 'expandtab' and 'smarttab' from rewriting it.
inline void appendInsertText(KeyedSequence& keys, std::string_view text) {
  size_t at;
  while ((at = text.find('\t')) != text.npos) {
    keys.append(text.substr(0, at));
    static const KeyedSequence literalTab{"<C-v><Tab>", {Key::Key_Ctrl, Key::Key_V, Key::Key_Tab}};
    keys += literalTab;
    text.remove_prefix(at + 1);
  }
  keys.append(text);
}

inline KeyedSequence computeIndentAdjustment(std::string_view from, std::string_view to,
                                            const IndentOptions& options) {
  KeyedSequence keys;
  if (to.starts_with(from)) {
    appendInsertText(keys, to.substr(from.size()));
  } else if (from.starts_with(to) && from.find('\t') == from.npos) {
    const int count = bsCountForIndent(from.size(), to.size(), options.backspaceWidth());
    if (count >= 0 && count <= 2 + std::ssize(to)) {
      keys.append(KeyedSequence::BS, count);
    } else {
      keys += KeyedSequence::CtrlU;
      appendInsertText(keys, to);
    }
  } else {
    keys += KeyedSequence::CtrlU;
    appendInsertText(keys, to);
  }
  return keys;
}

}  // export
