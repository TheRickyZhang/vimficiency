export module vf.Optimizer.CompositionOptimizer.CompositionStrategies;

import std.compat;
export import vf.Optimizer.BuildTypedCommands;
export import vf.Optimizer.CompositionOptimizer.CompositionSearchContext;
export import vf.Optimizer.DiffPlanner.DiffState;
export import vf.Types.BracketFlags;
export import vf.Types.Lines;
export import vf.Types.QuoteFlags;
export import vf.VimCore.VimCore;
export import vf.VimCore.VimEditUtils;
export import vf.VimCore.VimOptions;

export {

// =============================================================================
// Composition strategies — single source of truth
// =============================================================================
// What sequences telescope Navigate + Transform (and possibly Insert) for a
// given diff. Both consumers hand callbacks here:
//
//   - CompositionOptimizer (full A*) enqueues each strategy as a candidate
//     edit transition (with motion prefix found by an inner NavOptimizer).
//   - CompositionFrontier emits only the single structural action when the
//     current cursor is already in the strategy's activation region.
//
// Adding a new strategy here flows to both. If you find yourself adding a
// new structural emission to one consumer, it belongs in this header
// instead. The callbacks own DISPATCH; this file owns ENUMERATION.



namespace CompositionStrategies {

// One pure-insertion strategy. Multiple strategies may apply to the same diff
// (e.g. `I` line-scope + `i` point-scope for FNB insertions); each is
// reported as a separate callback invocation.
//
// Activation region: cursor lands inside the half-open range
// [beginCol, endCol) on `targetLine`.
//
// `structural` is the single Vim action that enters Insert at the right
// position (one of `i a I A o`). `insertCmd` is `structural` + typed
// payload + trailing `<Esc>` — the full batch-optimizer transition.
// CompositionFrontier emits `structural` only; CompositionOptimizer (batch
// A*) uses `insertCmd`.
struct Insertion {
  int targetLine;
  int beginCol;
  int endCol;
  std::string structural;
  std::string insertCmd;
  CursorPos goalPos;
};

// One bracket/quote shortcut. Many strategies per diff — one per
// (column, pair) seen by validQuoteMask / validBracketMask.
//
// Activation region: cursor at the enumerated `(line, col)`.
//
// `structural` is the single Vim action ('d'/'c' + 'i'/'a' modifier +
// bracket/quote char), e.g. `di"`, `ci(`. `body` is `structural` for pure
// deletions and `structural + insertedText + <Esc>` for replacements — the
// full batch-optimizer transition. CompositionFrontier emits `structural`
// only; CompositionOptimizer (batch A*) uses `body`.
struct BracketQuote {
  int line;
  int col;
  std::string structural;
  std::string body;
  CursorPos goalPos;
};

// Typed payload after the structural entry (trailing `<Esc>` included) and
// the Normal-mode cursor it leaves.
struct TypedInsertion {
  KeyedSequence typed;
  CursorPos exitCursor;
};

// `o` from `fromLine`; `body` is the insert minus its boundary newline.
inline TypedInsertion typedInsertionBelow(const Lines& lines, int fromLine,
                                          std::string_view body) {
  std::string_view sourceIndent = VimOptions::autoindent()
      ? leadingWhitespace(lines[fromLine])
      : std::string_view{};
  Lines insertLines = Lines::unflatten(std::string(body));
  return {buildTypedCommands(insertLines, sourceIndent),
          typedCommandsExitCursor(CursorPos(fromLine + 1, 0), insertLines)};
}

// `i a I A` at `insertPos`: the structural only picks the entry, the payload
// is the same for all of them.
inline TypedInsertion typedInsertionAt(const Lines& lines, CursorPos insertPos,
                                       const Lines& insertLines) {
  std::string_view lineText = lines[insertPos.line];
  std::string_view prefix = lineText.substr(0, insertPos.col);
  std::string_view suffix = lineText.substr(insertPos.col);
  return {buildTypedCommands(insertLines, "", prefix, suffix),
          typedCommandsExitCursor(insertPos, insertLines, suffix)};
}

// Trailing `\n` at col 0 of a later line: the line is wholly new content, so
// `o` is the only entry.
inline bool opensLineBelowOnly(const DiffState& diff) {
  return diff.isNewLineInsertion() && diff.beginPos.col == 0 &&
         diff.beginPos.line > 0;
}

// The single payload for a pure insertion once Insert has been entered: the
// `o` form when it is the only entry, else the column form (a leading-`\n`
// append is typed as `<CR>...` from the column, not via `o`).
inline TypedInsertion typedInsertion(const DiffState& diff, const Lines& lines) {
  if (opensLineBelowOnly(diff)) {
    return typedInsertionBelow(lines, diff.beginPos.line - 1,
                               diff.insertedTextBody());
  }
  return typedInsertionAt(lines, diff.beginPos, diff.insertedLines());
}

// Enumerate every Insertion strategy for `diff` against `lines`. No-op
// when `diff` is not a pure insertion.
template <class OnStrategy>
void enumerateInsertions(const DiffState& diff, const Lines& lines, OnStrategy&& cb) {
  if (!diff.isPureInsertion()) return;
  const CursorPos insertPos = diff.beginPos;
  const std::string& ins = diff.insertedText;

  // `o` opens a new line below and types the body. A new-line insertion reaches it
  // in two equivalent anchorings: a *trailing* `\n` at col 0 of a later line
  // (insert "X\n" before it), or a *leading* `\n` at the end of a line (insert
  // "\nX" after it) — the latter is also the only form for an end-of-buffer
  // append. Either way `o` runs from the line above the new line and the body is
  // the insert minus that one boundary newline.
  const bool trailingNl = opensLineBelowOnly(diff);
  const bool leadingNl = !ins.empty() && ins.front() == '\n' &&
                         insertPos.col == lines[insertPos.line].effectiveSize();
  if (trailingNl || leadingNl) {
    const int oFromLine = trailingNl ? insertPos.line - 1 : insertPos.line;
    const int oFromLineEnd = lines[oFromLine].effectiveSize();
    std::string_view body = trailingNl ? diff.insertedTextBody()
                                       : std::string_view(ins).substr(1);
    TypedInsertion below = typedInsertionBelow(lines, oFromLine, body);
    cb(Insertion{oFromLine, 0, oFromLineEnd, "o", "o" + below.typed.seq.str(),
                 below.exitCursor});
    if (trailingNl) return;
  }

  // I/A/i/a family.
  const int line = insertPos.line;
  const int fnb = VimCore::firstNonBlankColInLine(lines[line]);
  const int lineLen = lines.getSize(line);
  const int lineEnd = lines[line].effectiveSize();
  TypedInsertion at = typedInsertionAt(lines, insertPos, diff.insertedLines());
  const std::string escaped = at.typed.seq.str();

  // `I` lands at the first non-blank, or past the indent (col == size) on an
  // all-whitespace line.
  const int iCol = VimCore::isBlankLine(lines[line]) ? lineLen : fnb;
  if (insertPos.col == iCol) {
    cb(Insertion{line, 0, lineEnd, "I", "I" + escaped, at.exitCursor});
  }
  if (insertPos.col == lineLen) {
    cb(Insertion{line, 0, lineEnd, "A", "A" + escaped, at.exitCursor});
  }
  // `a` from the character before the insert point (col 0 on an empty line).
  if (insertPos.col > 0 || lineLen == 0) {
    const int aCol = std::max(insertPos.col - 1, 0);
    cb(Insertion{line, aCol, aCol + 1, "a", "a" + escaped, at.exitCursor});
  }
  // `i` needs the cursor on the insert column, which is unreachable one past
  // a non-empty line.
  if (insertPos.col < lineEnd) {
    cb(Insertion{line, insertPos.col, insertPos.col + 1,
                 "i", "i" + escaped, at.exitCursor});
  }
}

// Enumerate every BracketQuote strategy for `diff` against `bqContext`.
// No-op when `diff` is a pure insertion or `bqContext` was not computed
// for this diff's line (line < 0).
template <class OnStrategy>
void enumerateBracketQuotes(const DiffState& diff,
                            const Lines& lines,
                            const BracketQuoteContext& bqContext,
                            OnStrategy&& cb) {
  if (diff.isPureInsertion()) return;
  if (bqContext.line < 0) return;

  const bool pureDeletion = diff.isPureDeletion();
  const char textObjOp = pureDeletion ? 'd' : 'c';
  const std::string& insertedText = diff.insertedText;
  CursorPos goalPos = diff.beginPos;
  if (pureDeletion) {
    Lines after = lines;
    VimCore::deleteRangeAndUpdatePos(
        after, CharRange(diff.beginPos, diff.endPos), goalPos);
  } else {
    goalPos = typedCommandsExitCursor(diff.beginPos, diff.insertedLines());
  }

  auto structuralFor = [&](char structuralChar, char modifierCh) {
    std::string s(1, textObjOp);
    s += modifierCh;
    s += structuralChar;
    return s;
  };
  auto bodyFor = [&](const std::string& structural) {
    if (pureDeletion) return structural;
    return structural + insertedText + "<Esc>";
  };

  for (int col = 0; col < static_cast<int>(bqContext.validQuoteMask.size()); col++) {
    for (char q : QuoteFlags::ALL_QUOTES) {
      if (!bqContext.validQuoteMask[col].seen(q)) continue;
      std::string structural = structuralFor(q, bqContext.quoteModifier(q));
      cb(BracketQuote{
          bqContext.line, col, structural, bodyFor(structural), goalPos});
    }
  }
  for (int col = 0; col < static_cast<int>(bqContext.validBracketMask.size()); col++) {
    for (char b : BracketFlags::ALL_BRACKETS) {
      if (!bqContext.validBracketMask[col].seen(b)) continue;
      std::string structural = structuralFor(b, bqContext.bracketModifier(b));
      cb(BracketQuote{
          bqContext.line, col, structural, bodyFor(structural), goalPos});
    }
  }
}

}  // namespace CompositionStrategies

}  // export
