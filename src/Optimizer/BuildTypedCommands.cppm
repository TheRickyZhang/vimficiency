module;
#include <cassert>

export module vf.Optimizer.BuildTypedCommands;

import std.compat;
export import vf.Keyboard.KeyedSequence;
export import vf.Optimizer.Indentation;
export import vf.Types.CursorPos;
export import vf.Types.Lines;
export import vf.VimCore.VimOptions;

export {

inline KeyedSequence buildCollapseSequence(int totalLines, int cursorLine) {
  assert(cursorLine >= 0 && cursorLine < totalLines);
  KeyedSequence keys;
  keys.append(KeyedSequence::BS, cursorLine);
  keys.append(KeyedSequence::Del, totalLines - 1 - cursorLine);
  return keys;
}

inline CursorPos typedCommandsExitCursor(
    CursorPos insertPos,
    const Lines& goalLines,
    std::string_view suffix = "", const IndentOptions& options = {}) {
  const int suffixSpaces = options.autoindent ? IndentOptions::leadingIndent(suffix).size() : 0;

  if (goalLines.size() == 1) {
    int typedLen = goalLines.getSize(0);
    int col = typedLen == 0 ? insertPos.col : insertPos.col + typedLen - 1;
    return CursorPos(insertPos.line, col);
  }

  int finalLine = insertPos.line + goalLines.lastLine();
  int finalLineChars = goalLines.getSize(goalLines.lastLine()) + suffixSpaces;
  int col = std::max(0, finalLineChars - 1);
  return CursorPos(finalLine, col);
}

// Build the typed content string from goalLines, accounting for Neovim autoindent.
//
// Parameters:
//   goalLines          - the lines to type out
//   initialAutoindent  - indent provided on the first typed line (e.g. from cc's source line)
//   linePrefix         - text before edit region on first line (for computing line 1+ autoindent)
//   suffix             - text after edit region on last line. When typing multi-line content,
//                         <CR> pushes the suffix to a new line, and Neovim strips its leading
//                         whitespace during the split. We re-type those spaces before <Esc>.
//                         Example: "prefix|cursor|  suffix" -> type "a\nb" ->
//                           "prefixa"  /  "[autoindent]b[cursor]suffix" (spaces stripped)
//                         Not needed for single-line goals since no <CR> is typed.
//
// Returns KeyedSequence including trailing <Esc>.
inline KeyedSequence buildTypedCommands(
    const Lines &goalLines,
    std::string_view initialAutoindent = "", // indent on first typed line
    std::string_view linePrefix = "",
    std::string_view suffix = "",
    const IndentOptions& options = {}
) {
  KeyedSequence ks;
  std::string nextIndent;
  for (size_t i = 0; i < goalLines.size(); ++i) {
    const std::string_view line = goalLines[i];
    if (i > 0) {
      ks += KeyedSequence::CR;
      const std::string_view previous = goalLines[i - 1];
      if (i == 1 && !linePrefix.empty()) {
        const auto prefixIndent = IndentOptions::leadingIndent(linePrefix);
        nextIndent = prefixIndent.size() == linePrefix.size()
            ? options.openedIndent(std::string(prefixIndent) + std::string(IndentOptions::leadingIndent(previous)))
            : options.openedIndent(prefixIndent);
      } else {
        nextIndent = options.openedIndent(previous);
      }
    }
    const std::string_view indent = i == 0 ? initialAutoindent : std::string_view(nextIndent);
    if (indent.empty()) {
      appendInsertText(ks, line);
    } else {
      const auto goalIndent = IndentOptions::leadingIndent(line);
      // Untouched autoindent disappears on <Esc> or the next <CR>.
      if (!line.empty() && line == goalIndent && indent.starts_with(line)) {
        ks += computeIndentAdjustment(indent, {}, options);
        appendInsertText(ks, line);
      } else {
        ks += computeIndentAdjustment(indent, goalIndent, options);
        appendInsertText(ks, line.substr(goalIndent.size()));
      }
    }
  }

  if (options.autoindent && goalLines.size() > 1) {
    appendInsertText(ks, IndentOptions::leadingIndent(suffix));
  }
  ks += KeyedSequence::Esc;
  return ks;
}

}  // export
